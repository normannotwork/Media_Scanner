#include "scanner.h"
#include "server.h"
#include "media_state.h"
#include <iostream>
#include <csignal>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>

std::atomic<bool> keep_running{true};
HttpServer* global_server = nullptr;

void signalHandler(int signum) {
    std::cout << "\n[INFO] Signal " << signum << " received. Shutting down...\n";
    keep_running = false;
    if (global_server) global_server->stop();
}

std::string get_home_directory() {
    const char* home = getenv("HOME");
    if (home) return std::string(home);
    struct passwd* pw = getpwuid(getuid());
    return pw ? std::string(pw->pw_dir) : "/";
}

int main(int argc, char* argv[]) {
    std::string path = get_home_directory();
    int interval = 60;
    bool http_mode = false;
    int port = 1234;

    // Простейший ручной парсер аргументов (без внешних либ)
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--path" && i + 1 < argc) path = argv[++i];
        else if (arg == "--interval" && i + 1 < argc) interval = std::stoi(argv[++i]);
        else if (arg == "--http") http_mode = true;
        else if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    try {
        MediaState state;
        MediaScanner scanner(path, interval, http_mode, state);
        
        scanner.start();

        if (http_mode) {
            HttpServer server(port, state);
            global_server = &server;
            server.start(); // Блокирующий вызов сервера
        } else {
            // Если HTTP-сервер не нужен, просто ждем сигналов прерывания
            while (keep_running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }

        scanner.stop();
        
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
