#pragma once

class TimerFd {

private:
    int fd{-1};

public:
    TimerFd();
    ~TimerFd();

    TimerFd(const TimerFd&) = delete;
    TimerFd& operator=(const TimerFd&) = delete;

    bool start(int interval_seconds);

    void consume();

    int getFd() const;
};