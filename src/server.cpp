#include "server.h"
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <cstring>
#include <stdexcept>

#define MAX_EVENTS 10000

struct ScopedSocket {
    int fd;
    ~ScopedSocket() { if (fd >= 0) close(fd); }
};

HttpServer::HttpServer(int port, MediaState& state) 
    : server_fd_(-1), epoll_fd_(-1), port_(port), shared_state_(state) {
    
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) throw std::runtime_error("Socket creation failed");

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setNonBlocking(server_fd_); // Делаем серверный сокет неблокирующим

    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr *)&address, sizeof(address)) < 0) {
        throw std::runtime_error("Bind failed. Port might be in use.");
    }
    // SOMAXCONN позволяет ОС держать большую очередь входящих соединений
    if (listen(server_fd_, SOMAXCONN) < 0) {
        throw std::runtime_error("Listen failed");
    }

    // Создаем epoll event
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) throw std::runtime_error("epoll_create1 failed");
    struct epoll_event event;
    event.events = EPOLLIN; // Нас интересует только готовность к чтению (входящие коннекты)
    event.data.fd = server_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &event) < 0) {
        throw std::runtime_error("epoll_ctl failed on server_fd");
    }
}

HttpServer::~HttpServer() { stop(); }

void HttpServer::setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return;
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void HttpServer::start() {
    is_running_ = true;
    syslog(LOG_INFO, "[HTTP] Server listening on port %d using epoll", port_);

    struct epoll_event events[MAX_EVENTS];

    while (is_running_) {
        // Таймаут 1000 для того чтобы цикл мог проверить is_running_ при остановке службы
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, 1000);
        
        if (nfds < 0 && errno != EINTR) {
            syslog(LOG_ERR, "epoll_wait error");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == server_fd_) {
                // Новое входящее соединение
                while (true) {
                    int client_socket = accept(server_fd_, nullptr, nullptr);
                    if (client_socket < 0) {
                        break; // Больше нет входящих
                    }
                    // Защита от утечки FD, если epoll_ctl упадет забираем fd в ScopedSocket
                    ScopedSocket auto_close{client_socket}; 

                    setNonBlocking(client_socket);
                    struct epoll_event client_event{};

                    // Добавляем клиентский сокет в epoll для отслеживания готовности к чтению
                    client_event.events = EPOLLIN; 
                    client_event.data.fd = client_socket;

                    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_socket, &client_event) == 0) {
                        // Успешно добавили в epoll, забираем FD из-под авто-закрытия
                        auto_close.fd = -1; 
                    } else {
                        syslog(LOG_ERR, "Failed to add client to epoll");
                    }
                }
            } else {
                // Готовность к чтению от клиента
                handleClient(events[i].data.fd);
            }
        }
    }
}

void HttpServer::stop() {
    if (!is_running_) return;
    is_running_ = false;
    
    // Закрываем все сокеты и epoll
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
}

void HttpServer::handleClient(int client_socket) {
    ScopedSocket auto_close{client_socket};

    std::string request;
    char buffer[4096];
    bool headers_complete = false;

    while (true) {
        ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer));
        
        if (bytes_read > 0) {
            request.append(buffer, bytes_read);
            
            if (request.size() > 8192) {
                break; 
            }
            
            // Ищем конец HTTP-запроса
            if (request.find("\r\n\r\n") != std::string::npos) {
                headers_complete = true;
                break; 
            }
        } else if (bytes_read == 0) {
            // EOF
            break; // Клиент закрыл соединение
        } else {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; 
            }
            return; // Другая ошибка
        }
    }

    if (headers_complete) {
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

    const char* data_ptr = resp.data();
    size_t bytes_left = resp.length();

    // Отправляем данные в цикле, пока не уйдет весь объем
    while (bytes_left > 0) {
        ssize_t bytes_sent = send(client_socket, data_ptr, bytes_left, MSG_NOSIGNAL);
        
     if (bytes_sent > 0) {
            data_ptr += bytes_sent;
            bytes_left -= bytes_sent;
        } else if (bytes_sent == 0) {
            break; 
        } else {
            // хотим завыыершить соединение, если произошла ошибка, но не из-за прерывания или блокировки
            if (errno == EINTR) {
                continue;
            }
            // сокет не готов к отправке, нужно подождать
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000);
                continue;
            }
            break; 
        }
    }
}


