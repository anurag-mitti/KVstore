#include "epoll_server.h"
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <thread>
#include <atomic>
#include <vector>

constexpr int MAX_EVENTS = 10000;

EpollServer::EpollServer(uint16_t port, std::function<void(int)> on_read) : on_read_(on_read) {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    set_non_blocking(server_fd_);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    bind(server_fd_, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd_, SOMAXCONN);

    epoll_fd_ = epoll_create1(0);
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = server_fd_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &event);
}

EpollServer::~EpollServer() {
    close(server_fd_);
    close(epoll_fd_);
}

void EpollServer::set_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void EpollServer::start() {
    running_ = true;
    
    int num_threads = std::thread::hardware_concurrency();
    if (num_threads <= 0) num_threads = 4; // fallback just in case

    std::vector<int> worker_epolls(num_threads);
    std::vector<std::thread> workers;

    // spin up a thread pool 
    for (int i = 0; i < num_threads; ++i) {
        worker_epolls[i] = epoll_create1(0);
        
        workers.emplace_back([this, epfd = worker_epolls[i]]() {
            std::vector<epoll_event> events(MAX_EVENTS);
            while (running_) {
                int num = epoll_wait(epfd, events.data(), MAX_EVENTS, 100);
                for (int j = 0; j < num; ++j) {
                    on_read_(events[j].data.fd);
                }
            }
        });
    }

    std::cout << "multi-reactor thread pool started. workers: " << num_threads << std::endl;

    std::atomic<int> next_worker{0};
    std::vector<epoll_event> main_events(10);

    // main loop accepts connections and hands them off
    while (running_) {
        int num = epoll_wait(epoll_fd_, main_events.data(), 10, -1);
        for (int i = 0; i < num; ++i) {
            if (main_events[i].data.fd == server_fd_) {
                sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
                
                if (client_fd >= 0) {
                    set_non_blocking(client_fd);
                    epoll_event ev{};
                    ev.events = EPOLLIN | EPOLLET; // edge triggered
                    ev.data.fd = client_fd;
                    
                    // round robin lb
                    int target_epoll = worker_epolls[next_worker++ % num_threads];
                    epoll_ctl(target_epoll, EPOLL_CTL_ADD, client_fd, &ev);
                }
            }
        }
    }

    running_ = false;
    for (auto& w : workers) w.join();
    for (int epfd : worker_epolls) close(epfd);
}

void EpollServer::stop() {
    running_ = false;
}
