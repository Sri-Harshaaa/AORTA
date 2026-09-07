#pragma once

class Socket {

private:
    int fd{-1};

public:
    Socket();
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    bool bindAndListen(int port);

    int getFd() const;

    static bool setNonBlocking(int fd);
    static bool setNoDelay(int fd);
};