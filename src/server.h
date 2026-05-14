#pragma once
#include "media_state.h"
#include <string>
#include <atomic>

class HttpServer {
    private:
        int server_fd_;
        int port_;
        std::atomic<bool> is_running_{false};
        MediaState& shared_state_;

    public:
        HttpServer(int port, MediaState& state);
        ~HttpServer();
        
        void start();
        void stop();

    private:
        void handleClient(int client_socket);
        void sendHttpResponse(int client_socket, const std::string& status, const std::string& content_type, const std::string& body);
};