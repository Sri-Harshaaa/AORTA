#include "net/TimerFd.hpp"

#include <iostream>
#include <cstdint>

#include <sys/timerfd.h>
#include <unistd.h>

TimerFd::TimerFd() {
    fd = timerfd_create(
        CLOCK_MONOTONIC,
        TFD_NONBLOCK | TFD_CLOEXEC
    );

    if(fd == -1) {
        std::cerr << "timerfd_create() failed" << std::endl;
        return;
    }

    std::cout << "TimerFd created FD: " << fd << std::endl;
}


TimerFd::~TimerFd() {
    if(fd != -1) {
        close(fd);
        std::cout << "TimerFd closed FD: " << fd << std::endl;
    }
}


bool TimerFd::start(int interval_seconds) {
    if(fd == -1 || interval_seconds <= 0) {
        return false;
    }

    struct itimerspec timer;

    timer.it_value.tv_sec = interval_seconds;
    timer.it_value.tv_nsec = 0;

    timer.it_interval.tv_sec = interval_seconds;
    timer.it_interval.tv_nsec = 0;

    if(timerfd_settime(
        fd,
        0,
        &timer,
        nullptr
    ) == -1) {
        std::cerr << "timerfd_settime() failed" << std::endl;
        return false;
    }

    return true;
}


void TimerFd::consume() {
    if(fd == -1) {
        return;
    }

    std::uint64_t expirations;

    read(
        fd,
        &expirations,
        sizeof(expirations)
    );
}


int TimerFd::getFd() const {
    return fd;
}