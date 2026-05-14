#include "scanner.h"
#include "server.h"
#include "media_state.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>
#include <mutex>
#include <condition_variable>

std::mutex main_mutex;
std::condition_variable main_cv;
bool keep_running = true;

HttpServer* global_server = nullptr;

void signalHandler(int signum) {
    std::cout << "\n[INFO] Signal " << signum << " received. Initiating graceful shutdown...\n";
    {
        std::lock_guard<std::mutex> lock(main_mutex);
        keep_running = false;
    }
    main_cv.notify_all();
    if (global_server) global_server->stop(); // Провоцирует выход из accept()
}

std::string get_home_directory() {
    const char* home = getenv("HOME");
    if (home) return home;
    struct passwd* pw = getpwuid(getuid());
    return pw ? pw->pw_dir : "/";
}

int main(int argc, char* argv[]) {
    // Игнорируем SIGPIPE на уровне процесса
    signal(SIGPIPE, SIG_IGN); 
    
    std::string path = get_home_directory();
    int interval = 60;
    bool http_mode = false;
    int port = 1234;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        try {
            if (arg == "--path" && i + 1 < argc) path = argv[++i];
            else if (arg == "--interval" && i + 1 < argc) interval = std::stoi(argv[++i]);
            else if (arg == "--http") http_mode = true;
            else if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
        } catch (const std::exception&) {
            // Если вместо цифр передали буквы (--port abc)
            std::cerr << "[ERROR] Invalid numeric argument provided for " << arg << "\n";
            return 1;
        }
    }

    // системные прерывания (Ctrl+C, kill, docker stop)
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    try {
        MediaState state;
        MediaScanner scanner(path, interval, http_mode, state);
        scanner.start();

        if (http_mode) {
            HttpServer server(port, state);
            global_server = &server;
            server.start(); // Цикл accept() блокирует поток. Работает до вызова server.stop()
        } else { 
            // Поток спит, пока Ctrl+C не разбудит его через main_cv
            std::unique_lock<std::mutex> lock(main_mutex);
            main_cv.wait(lock, [] { return !keep_running; });
        }

        scanner.stop(); // Дожидаемся чистого завершения дочернего потока
        
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}