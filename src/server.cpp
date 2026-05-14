#include "server.h"
#include <iostream>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

// Гарантирует, что файловый дескриптор сокета закроется
struct ScopedSocket {
    int fd;
    ~ScopedSocket() { if (fd >= 0) close(fd); }
};

HttpServer::HttpServer(int port, MediaState& state) 
    : server_fd_(-1), port_(port), shared_state_(state) {
    
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) throw std::runtime_error("Socket creation failed");

    // SO_REUSEADDR позволяет мгновенно перезапустить сервер после падения/остановкИ
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr *)&address, sizeof(address)) < 0) {
        throw std::runtime_error("Bind failed. Port might be in use.");
    }
    if (listen(server_fd_, 10) < 0) {
        throw std::runtime_error("Listen failed");
    }
}

HttpServer::~HttpServer() { stop(); }

void HttpServer::start() {
    is_running_ = true;
    std::cout << "[HTTP] Server listening on http://localhost:" << port_ << "/media_files\n";

    while (is_running_) {
        int client_socket = accept(server_fd_, nullptr, nullptr);
        
        if (!is_running_) {
            if (client_socket >= 0) close(client_socket);
            break;
        }

        if (client_socket >= 0) {
            // когда клиент открывает сокет, но ничего не присылает
            // Сервер разорвет соединение через 5 секунд тишины, освободив поток
            struct timeval tv{5, 0}; 
            setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            
            std::thread(&HttpServer::handleClient, this, client_socket).detach();
        }
    }
}

void HttpServer::stop() {
    if (!is_running_) return;
    is_running_ = false;
    if (server_fd_ >= 0) {
        shutdown(server_fd_, SHUT_RDWR); // Моментально прерывает блокирующий accept()
        close(server_fd_);
        server_fd_ = -1;
    }
}

void HttpServer::handleClient(int client_socket) {
    ScopedSocket auto_close{client_socket};

    char buffer[1024];
    // Читаем один раз, так как нам нужен только заголовок GET для базового роутинга
    ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        std::string request(buffer);
        
        if (request.find("GET /media_files") == 0) {
            sendHttpResponse(client_socket, "200 OK", "application/json", shared_state_.get_json());
        } else {
            sendHttpResponse(client_socket, "404 Not Found", "text/plain", "Endpoint not found. Use GET /media_files");
        }
    }
}

void HttpServer::sendHttpResponse(int client_socket, const std::string& status, const std::string& content_type, const std::string& body) {
    std::string resp = "HTTP/1.1 " + status + "\r\n"
                       "Content-Type: " + content_type + "\r\n"
                       "Content-Length: " + std::to_string(body.length()) + "\r\n"
                       "Connection: close\r\n\r\n" + body;
                       
    // MSG_NOSIGNAL предотвращает краш (сигнал SIGPIPE), если клиент нажал ОТМЕНА
    send(client_socket, resp.data(), resp.length(), MSG_NOSIGNAL);
}
