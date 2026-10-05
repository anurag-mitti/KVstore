#pragma once
#include <sys/epoll.h>
#include <functional>
#include <vector>
#include <cstdint>

class EpollServer {
    int server_fd_;
    int epoll_fd_;
    bool running_{false};

    std::function<void(int client_fd)> on_read_;
    void set_non_blocking(int fd);

public:
    EpollServer(uint16_t port, std::function<void(int)> on_read);
    ~EpollServer();

    void start();
    void stop();
};
