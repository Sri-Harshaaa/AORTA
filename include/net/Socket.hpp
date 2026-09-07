#pragma once

class Socket {

private:
    int fd{-1};

public:
    /*
     * The kernel clamps the backlog to net.core.somaxconn, so asking for more
     * than the system allows is harmless. The old value of 128 was well below
     * what a high-concurrency test needs and silently capped the accept rate.
     */
    static constexpr int DEFAULT_BACKLOG = 4096;

    explicit Socket(bool non_blocking = true);

    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    bool bindAndListen(
        int port,
        int backlog = DEFAULT_BACKLOG
    );

    int getFd() const;

    static bool setNonBlocking(int fd);
    static bool setNoDelay(int fd);

    static bool setReceiveTimeout(int fd, int seconds);
    static bool setSendTimeout(int fd, int seconds);
};
