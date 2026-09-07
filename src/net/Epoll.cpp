#include "net/Epoll.hpp"

#include <iostream>

#include <sys/epoll.h>
#include <unistd.h>

Epoll::Epoll() {
    epoll_fd = epoll_create1(0);

    if(epoll_fd == -1) {
        std::cerr << "epoll_create1() failed" << std::endl;
        return;
    }

    std::cout << "Epoll created FD: " << epoll_fd << std::endl;
}

Epoll::~Epoll() {
    if(epoll_fd != -1) {
        close(epoll_fd);
        std::cout << "Epoll closed FD: " << epoll_fd << std::endl;
    }
}

bool Epoll::add(int fd, uint32_t events) {
    struct epoll_event event;

    event.events = events;
    event.data.fd = fd;

    int result = epoll_ctl(
        epoll_fd,
        EPOLL_CTL_ADD,
        fd,
        &event
    );

    if(result == -1) {
        std::cerr << "epoll_ctl(ADD) failed" << std::endl;
        return false;
    }

    return true;
}

bool Epoll::modify(int fd, uint32_t events) {
    struct epoll_event event;

    event.events = events;
    event.data.fd = fd;

    int result = epoll_ctl(
        epoll_fd,
        EPOLL_CTL_MOD,
        fd,
        &event
    );

    if(result == -1) {
        std::cerr << "epoll_ctl(MOD) failed" << std::endl;
        return false;
    }

    return true;
}

bool Epoll::remove(int fd) {
    int result = epoll_ctl(
        epoll_fd,
        EPOLL_CTL_DEL,
        fd,
        nullptr
    );

    if(result == -1) {
        std::cerr << "epoll_ctl(DEL) failed" << std::endl;
        return false;
    }

    return true;
}

int Epoll::wait(
    struct epoll_event* events,
    int max_events,
    int timeout
) {
    return epoll_wait(
        epoll_fd,
        events,
        max_events,
        timeout
    );
}

int Epoll::getFd() const {
    return epoll_fd;
}