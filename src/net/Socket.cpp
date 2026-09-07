#include "net/Socket.hpp"

#include <iostream>

#include <sys/time.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>

Socket::Socket(bool non_blocking) {
    fd = socket(AF_INET, SOCK_STREAM, 0);

    if(fd == -1) {
        std::cerr << "socket() failed" << std::endl;
        return;
    }

    int option = 1;

    int result = setsockopt(
        fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &option,
        sizeof(option)
    );

    if(result == -1) {
        std::cerr << "SO_REUSEADDR failed" << std::endl;
        close(fd);
        fd = -1;
        return;
    }

    result = setsockopt(
        fd,
        SOL_SOCKET,
        SO_REUSEPORT,
        &option,
        sizeof(option)
    );

    if(result == -1) {
        std::cerr << "SO_REUSEPORT failed" << std::endl;
        close(fd);
        fd = -1;
        return;
    }

    if(non_blocking && !setNonBlocking(fd)) {
        close(fd);
        fd = -1;
        return;
    }
}

Socket::~Socket() {
    if(fd != -1) {
        close(fd);
    }
}

bool Socket::bindAndListen(int port, int backlog) {
    if(fd == -1) {
        return false;
    }

    struct sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    int result = bind(
        fd,
        (struct sockaddr*)&address,
        sizeof(address)
    );

    if(result == -1) {
        std::cerr << "bind() failed" << std::endl;
        return false;
    }

    result = listen(fd, backlog);

    if(result == -1) {
        std::cerr << "listen() failed" << std::endl;
        return false;
    }

    return true;
}

int Socket::getFd() const {
    return fd;
}

bool Socket::setNonBlocking(int fd) {
    int flags = fcntl(
        fd,
        F_GETFL,
        0
    );

    if(flags == -1) {
        std::cerr << "fcntl(F_GETFL) failed" << std::endl;
        return false;
    }

    if(fcntl(
        fd,
        F_SETFL,
        flags | O_NONBLOCK
    ) == -1) {
        std::cerr << "fcntl(F_SETFL) failed" << std::endl;
        return false;
    }

    return true;
}

bool Socket::setNoDelay(int fd) {
    int option = 1;

    if(setsockopt(
        fd,
        IPPROTO_TCP,
        TCP_NODELAY,
        &option,
        sizeof(option)
    ) == -1) {
        std::cerr << "TCP_NODELAY failed" << std::endl;
        return false;
    }

    return true;
}


bool Socket::setReceiveTimeout(int fd, int seconds) {
    struct timeval timeout{};

    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    return setsockopt(
        fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout)
    ) != -1;
}


bool Socket::setSendTimeout(int fd, int seconds) {
    struct timeval timeout{};

    timeout.tv_sec = seconds;
    timeout.tv_usec = 0;

    return setsockopt(
        fd,
        SOL_SOCKET,
        SO_SNDTIMEO,
        &timeout,
        sizeof(timeout)
    ) != -1;
}
