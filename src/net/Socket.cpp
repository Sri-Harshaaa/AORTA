#include "net/Socket.hpp"

#include <iostream>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>

Socket::Socket() {
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

    if(!setNonBlocking(fd)) {
        close(fd);
        fd = -1;
        return;
    }

    std::cout << "Socket created FD: " << fd << std::endl;
}

Socket::~Socket() {
    if(fd != -1) {
        close(fd);
        std::cout << "Socket closed FD: " << fd << std::endl;
    }
}

bool Socket::bindAndListen(int port) {
    if(fd == -1) {
        return false;
    }

    struct sockaddr_in address;

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

    std::cout << "bind() successful" << std::endl;

    result = listen(fd, 128);

    if(result == -1) {
        std::cerr << "listen() failed" << std::endl;
        return false;
    }

    std::cout << "listen() successful" << std::endl;

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