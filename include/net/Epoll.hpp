#pragma once

#include <sys/epoll.h>

class Epoll {

private:
    int epoll_fd{-1};

public:
    Epoll();
    ~Epoll();

    Epoll(const Epoll&) = delete;
    Epoll& operator=(const Epoll&) = delete;

    bool add(int fd, uint32_t events);
    bool modify(int fd, uint32_t events);
    bool remove(int fd);

    int wait(
        struct epoll_event* events,
        int max_events,
        int timeout
    );

    int getFd() const;
};