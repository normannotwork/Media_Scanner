#include "scanner.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

MediaScanner::MediaScanner(const std::string& root, int interval, bool http_mode, MediaState& state)
    : root_path_(root), interval_sec_(interval), http_mode_(http_mode), shared_state_(state) {
    init_extensions();
}

MediaScanner::~MediaScanner() { stop(); }

void MediaScanner::init_extensions() {
    ext_map_ = {
        {".jpg", "images"}, {".jpeg", "images"}, {".png", "images"}, {".gif", "images"}, {".bmp", "images"}, {".webp", "images"},
        {".mp3", "audio"}, {".wav", "audio"}, {".ogg", "audio"}, {".flac", "audio"}, {".m4a", "audio"},
        {".mp4", "video"}, {".avi", "video"}, {".mkv", "video"}, {".mov", "video"}, {".webm", "video"}
    };
}

std::string MediaScanner::to_lower(const std::string& str) {
    std::string lower_str = str;
    // Приведено к unsigned char иначе выхода за пределы таблицы ASCII
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), 
                   [](unsigned char c) { return std::tolower(c); });
    return lower_str;
}

void MediaScanner::start() {
    std::error_code ec;
    if (!fs::exists(root_path_, ec) || !fs::is_directory(root_path_, ec)) {
        throw std::runtime_error("Directory does not exist or inaccessible: " + root_path_);
    }
    
    is_running_ = true;
    worker_thread_ = std::thread(&MediaScanner::run, this);
    std::cout << "[SCANNER] Started scanning '" << root_path_ << "' every " << interval_sec_ << " seconds.\n";
}

void MediaScanner::stop() {
    if (!is_running_) return;
    is_running_ = false;
    cv_.notify_all(); //Будим поток, если он в состоянии ожидания таймера
    if (worker_thread_.joinable()) worker_thread_.join();
}

void MediaScanner::run() {
    while (is_running_) {
        std::string json_result = scan_directory();
        
        if (http_mode_) {
            shared_state_.set_json(json_result);
        } else {
            save_to_file(json_result);
        }

        std::unique_lock<std::mutex> lock(cv_mutex_);
        // Поток засыпает на interval_sec_. Если за это время is_running_ станет false, он проснется досрочно.
        cv_.wait_for(lock, std::chrono::seconds(interval_sec_), [this] { return !is_running_.load(); });
    }
}

std::string MediaScanner::scan_directory() {
    json j = {{"audio", json::array()}, {"video", json::array()}, {"images", json::array()}};

    // Флаг избавляет от исключений на системных папках
    auto options = fs::directory_options::skip_permission_denied;
    std::error_code dir_ec;

    for (auto it = fs::recursive_directory_iterator(root_path_, options, dir_ec); 
         it != fs::recursive_directory_iterator(); 
         it.increment(dir_ec)) {
        
        if (dir_ec) {
            dir_ec.clear(); // Сбрасываем ошибку (например, битый symlink) и идем дальше
            continue;
        }

        auto& entry = *it;
        std::error_code file_ec;
        if (!entry.is_regular_file(file_ec) || file_ec) continue;

        std::string ext = to_lower(entry.path().extension().string());
        auto ext_it = ext_map_.find(ext);
        
        if (ext_it != ext_map_.end()) {
            //гарантирует прямые слеши '/' в пути
            std::string rel_path = fs::relative(entry.path(), root_path_, file_ec).generic_string();
            if (!file_ec) {
                j[ext_it->second].push_back(rel_path);
            }
        }
    }

    // error_handler_t::replace заменяет невалидные UTF-8 символы в именах файлов (возникают из-за кривых кодировок ОС) 
    return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

void MediaScanner::save_to_file(const std::string& json_data) {
    const char* home = getenv("HOME");
    if (!home) return;
    
    fs::path filepath = fs::path(home) / "media_files.json";
    std::ofstream out(filepath);
    if (out.is_open()) {
        out << json_data;
        std::cout << "[SCANNER] Result saved to " << filepath << "\n";
    }
}