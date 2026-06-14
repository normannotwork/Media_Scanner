#include "scanner.h"
#include "server.h"
#include "media_state.h"
#include <iostream>
#include <fstream>
#include <csignal>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>
#include <syslog.h>
#include <mutex>
#include <condition_variable>


std::mutex main_mutex;
std::condition_variable main_cv;
bool keep_running = true;

// Глобальный указатель для доступа из обработчика сигналов
HttpServer* global_server = nullptr;

void signalHandler(int signum) {
    syslog(LOG_INFO, "Signal %d received. Initiating graceful shutdown...", signum);
    {
        std::lock_guard<std::mutex> lock(main_mutex);
        keep_running = false;
    }
    main_cv.notify_all();
    if (global_server) global_server->stop();
}

std::string get_home_directory() {
    const char* home = getenv("HOME");
    if (home) return home;
    struct passwd* pw = getpwuid(getuid());
    return pw ? pw->pw_dir : "/";
}

// Функция для загрузки конфигурации из файла
void load_config(const std::string& filepath, std::string& path, int& interval, bool& http_mode, int& port) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        syslog(LOG_WARNING, "Config file %s not found, using defaults", filepath.c_str());
        return;
    }
    std::string line;

    auto trim = [](std::string& s) {
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);
    };

    while (std::getline(file, line)) {
        trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto delim = line.find('=');
        if (delim == std::string::npos) continue;
        
        std::string key = line.substr(0, delim);
        std::string val = line.substr(delim + 1);
        trim(key); trim(val);
        
        try {
            if (key == "path") path = val;
            else if (key == "interval") interval = std::stoi(val);
            else if (key == "http_mode") http_mode = (val == "true" || val == "1");
            else if (key == "port") port = std::stoi(val);
        } catch (const std::exception& e) {
            syslog(LOG_ERR, "Invalid config value for key '%s': %s", key.c_str(), val.c_str());
        }
    }
    syslog(LOG_INFO, "Config loaded successfully from %s", filepath.c_str());
}

int main(int argc, char* argv[]) {
    // Инициализация системного журнала
    openlog("MediaScanner", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "Starting MediaScanner daemon...");

    signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::string path = get_home_directory();
    int interval = 60;
    bool http_mode = true; // По умолчанию включено для службы
    int port = 1234;
    std::string config_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
            load_config(config_path, path, interval, http_mode, port);
        }
    }
    try {
        // Инициализация общего состояния для сканера и сервера
        MediaState state;
        MediaScanner scanner(path, interval, http_mode, state);
        // Запуск сканера в отдельном потоке
        scanner.start();

        if (http_mode) {
            HttpServer server(port, state);
            global_server = &server;
            // Запуск сервера блокирует основной поток, но это нормально для демона
            server.start(); // epoll_wait блокирует поток
        } else { 
            std::unique_lock<std::mutex> lock(main_mutex);
            main_cv.wait(lock, [] { return !keep_running; });
        }

        scanner.stop();
        syslog(LOG_INFO, "Daemon shut down cleanly.");
    } catch (const std::exception& e) {
        syslog(LOG_ERR, "FATAL ERROR: %s", e.what());
        closelog();
        return 1;
    }

    closelog();
    return 0;
}