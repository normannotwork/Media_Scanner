#include "scanner.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

MediaScanner::MediaScanner(const std::string& root, int interval, bool http_mode, MediaState& state)
    : root_path_(root), interval_sec_(interval), http_mode_(http_mode), shared_state_(state) {
    init_extensions();
}

MediaScanner::~MediaScanner() { stop(); }

void MediaScanner::init_extensions() {
    std::vector<std::string> images = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".tiff", ".webp"};
    std::vector<std::string> audio = {".mp3", ".wav", ".ogg", ".flac", ".m4a"};
    std::vector<std::string> video = {".mp4", ".avi", ".mkv", ".mov", ".wmv", ".flv", ".webm"};

    for (const auto& ext : images) ext_map_[ext] = "images";
    for (const auto& ext : audio)  ext_map_[ext] = "audio";
    for (const auto& ext : video)  ext_map_[ext] = "video";
}

std::string MediaScanner::to_lower(const std::string& str) {
    std::string lower_str = str;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);
    return lower_str;
}

void MediaScanner::start() {
    if (!fs::exists(root_path_) || !fs::is_directory(root_path_)) {
        throw std::runtime_error("Directory does not exist: " + root_path_);
    }
    is_running_ = true;
    worker_thread_ = std::thread(&MediaScanner::run, this);
    std::cout << "[SCANNER] Started scanning '" << root_path_ << "' every " << interval_sec_ << " seconds.\n";
}

void MediaScanner::stop() {
    is_running_ = false;
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

        // Прерываемый сон для корректного выхода
        for (int i = 0; i < interval_sec_ * 10 && is_running_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

std::string MediaScanner::scan_directory() {
    json j;
    j["audio"] = json::array();
    j["video"] = json::array();
    j["images"] = json::array();

    // skip_permission_denied - спасает от крашей при чтении root-директорий и кэша
    auto options = fs::directory_options::skip_permission_denied;
    
    try {
        for (const auto& entry : fs::recursive_directory_iterator(root_path_, options)) {
            if (!entry.is_regular_file()) continue;

            std::string ext = to_lower(entry.path().extension().string());
            auto it = ext_map_.find(ext);
            
            if (it != ext_map_.end()) {
                std::string category = it->second;
                // Формируем относительный путь
                std::string rel_path = fs::relative(entry.path(), root_path_).string();
                j[category].push_back(rel_path);
            }
            
            /*
            if (it != ext_map_.end()) {
                std::string category = it->second;
                std::string rel_path = fs::relative(entry.path(), root_path_).string();
                
                uintmax_t size = 0;
                uint64_t mtime = 0;
                std::error_code ec; // Не бросаем исключения на битых файлах
                if (entry.is_regular_file(ec) && !ec) {
                    size = entry.file_size(ec);
                    auto ftime = entry.last_write_time(ec);
                    if (!ec) {
                        // Безопасное приведение к system_clock для C++17
                        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                        mtime = std::chrono::system_clock::to_time_t(sctp);
                    }
                }

                j[category].push_back({
                    {"path", rel_path},
                    {"size", size},
                    {"mtime", mtime}
                });
            }
            */
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "[SCANNER] FS Error: " << e.what() << '\n';
    }

    return j.dump();
}

void MediaScanner::save_to_file(const std::string& json_data) {
    const char* home = getenv("HOME");
    if (!home) return;
    
    fs::path filepath = fs::path(home) / "media_files.json";
    std::ofstream out(filepath);
    if (out.is_open()) {
        out << json_data;
        out.close();
        std::cout << "[SCANNER] Result saved to " << filepath << "\n";
    } else {
        std::cerr << "[SCANNER] Failed to open " << filepath << " for writing\n";
    }
}
