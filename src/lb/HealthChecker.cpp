#include "lb/HealthChecker.hpp"

#include <algorithm>
#include <cerrno>
#include <iostream>
#include <utility>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

HealthChecker::~HealthChecker() {
    for(auto& [fd, check] : checks) {
        (void)check;
        close(fd);
    }

    checks.clear();
}

void HealthChecker::start(
    BackendPool& pool,
    Epoll& epoll
) {
    if(active()) {
        return;
    }

    backend_pool = &pool;

    for(std::size_t index = 0; index < pool.size(); ++index) {

        Backend& backend =
            pool.getBackend(index);

        addrinfo hints{};

        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* result = nullptr;

        const std::string port_string =
            std::to_string(backend.port);

        const int resolve_result =
            getaddrinfo(
                backend.host.c_str(),
                port_string.c_str(),
                &hints,
                &result
            );

        if(
            resolve_result != 0 ||
            result == nullptr
        ) {
            backend.consecutive_failures++;

            if(backend.consecutive_failures >= 2) {
                backend.healthy = false;
            }

            continue;
        }

        int fd = socket(
            result->ai_family,
            result->ai_socktype |
            SOCK_NONBLOCK |
            SOCK_CLOEXEC,
            result->ai_protocol
        );

        if(fd == -1) {

            freeaddrinfo(result);

            backend.consecutive_failures++;

            if(backend.consecutive_failures >= 2) {
                backend.healthy = false;
            }

            continue;
        }

        Check check;

        check.fd = fd;
        check.backend_index = index;

        check.request =
            "GET /health HTTP/1.1\r\n"
            "Host: " + backend.host + "\r\n"
            "Connection: close\r\n"
            "\r\n";

        check.deadline =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(
                CONNECT_TIMEOUT_MS
            );

        const int connect_result =
            connect(
                fd,
                result->ai_addr,
                result->ai_addrlen
            );

        freeaddrinfo(result);

        if(connect_result == 0) {

            check.state =
                State::Writing;

            check.deadline =
                std::chrono::steady_clock::now() +
                std::chrono::milliseconds(
                    RESPONSE_TIMEOUT_MS
                );

        } else if(errno == EINPROGRESS) {

            check.state =
                State::Connecting;

        } else {

            close(fd);

            backend.consecutive_failures++;

            if(backend.consecutive_failures >= 2) {
                backend.healthy = false;
            }

            continue;
        }

        const uint32_t events =
            EPOLLIN |
            EPOLLOUT |
            EPOLLERR |
            EPOLLHUP;

        if(!epoll.add(fd, events)) {

            close(fd);

            backend.consecutive_failures++;

            if(backend.consecutive_failures >= 2) {
                backend.healthy = false;
            }

            continue;
        }

        checks.emplace(
            fd,
            std::move(check)
        );
    }
}

void HealthChecker::handleEvent(
    int fd,
    uint32_t events,
    Epoll& epoll
) {
    if(!handles(fd)) {
        return;
    }

    /*
     * EPOLLIN and EPOLLHUP can arrive together.
     *
     * A backend using:
     *
     *     Connection: close
     *
     * can send the complete HTTP response and then close
     * its socket. Linux may report EPOLLIN | EPOLLHUP for
     * the same event.
     *
     * Therefore readable/writable state must be processed
     * before treating HUP/ERR as a failure.
     */

    auto it = checks.find(fd);

    if(it == checks.end()) {
        return;
    }

    Check& check = it->second;

    if(
        check.state == State::Connecting &&
        (events & EPOLLOUT)
    ) {
        handleConnect(
            check,
            epoll
        );

        if(
            checks.find(fd) ==
            checks.end()
        ) {
            return;
        }
    }

    it = checks.find(fd);

    if(it == checks.end()) {
        return;
    }

    Check& current = it->second;

    if(
        current.state == State::Writing &&
        (events & EPOLLOUT)
    ) {
        handleWrite(
            current,
            epoll
        );

        if(
            checks.find(fd) ==
            checks.end()
        ) {
            return;
        }
    }

    it = checks.find(fd);

    if(it == checks.end()) {
        return;
    }

    Check& reading = it->second;

    if(
        reading.state == State::Reading &&
        (events & EPOLLIN)
    ) {
        handleRead(
            reading,
            epoll
        );

        if(
            checks.find(fd) ==
            checks.end()
        ) {
            return;
        }
    }

    /*
     * Only treat HUP/ERR as failure if the check is still
     * alive after processing the useful event.
     *
     * If handleRead() received a valid 200 response,
     * finishCheck() already removed the check above.
     */
    if(events & (
        EPOLLERR |
        EPOLLHUP
    )) {
        failCheck(
            fd,
            epoll
        );
    }
}

bool HealthChecker::handles(
    int fd
) const {
    return checks.find(fd) != checks.end();
}

bool HealthChecker::active() const {
    return !checks.empty();
}

int HealthChecker::nextTimeoutMs() const {
    if(checks.empty()) {
        return -1;
    }

    const auto now =
        std::chrono::steady_clock::now();

    auto earliest =
        checks.begin()->second.deadline;

    for(const auto& [fd, check] : checks) {
        (void)fd;

        earliest =
            std::min(
                earliest,
                check.deadline
            );
    }

    if(earliest <= now) {
        return 0;
    }

    const auto remaining =
        std::chrono::duration_cast<
            std::chrono::milliseconds
        >(earliest - now);

    return static_cast<int>(
        remaining.count()
    );
}

void HealthChecker::expire(
    Epoll& epoll
) {
    const auto now =
        std::chrono::steady_clock::now();

    std::vector<int> expired;

    for(const auto& [fd, check] : checks) {

        if(check.deadline <= now) {
            expired.push_back(fd);
        }
    }

    for(int fd : expired) {
        failCheck(
            fd,
            epoll
        );
    }
}

void HealthChecker::cancelAll(
    Epoll& epoll
) {
    for(auto& [fd, check] : checks) {
        (void)check;

        epoll.remove(fd);
        close(fd);
    }

    checks.clear();
}

void HealthChecker::handleConnect(
    Check& check,
    Epoll& epoll
) {
    int socket_error = 0;

    socklen_t size =
        sizeof(socket_error);

    if(
        getsockopt(
            check.fd,
            SOL_SOCKET,
            SO_ERROR,
            &socket_error,
            &size
        ) == -1 ||
        socket_error != 0
    ) {

        failCheck(
            check.fd,
            epoll
        );

        return;
    }

    check.state =
        State::Writing;

    check.deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(
            RESPONSE_TIMEOUT_MS
        );
}

void HealthChecker::handleWrite(
    Check& check,
    Epoll& epoll
) {
    while(
        check.write_offset <
        check.request.size()
    ) {

        const ssize_t sent =
            send(
                check.fd,
                check.request.data() +
                    check.write_offset,
                check.request.size() -
                    check.write_offset,
                MSG_NOSIGNAL
            );

        if(sent > 0) {

            check.write_offset +=
                static_cast<std::size_t>(
                    sent
                );

            continue;
        }

        if(sent == -1 && errno == EINTR) {
            continue;
        }

        if(
            sent == -1 &&
            (
                errno == EAGAIN ||
                errno == EWOULDBLOCK
            )
        ) {
            return;
        }

        failCheck(
            check.fd,
            epoll
        );

        return;
    }

    check.state =
        State::Reading;

    check.deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(
            RESPONSE_TIMEOUT_MS
        );
}

void HealthChecker::handleRead(
    Check& check,
    Epoll& epoll
) {
    char buffer[1024];

    while(
        check.response.size() <
        MAX_RESPONSE_SIZE
    ) {

        const std::size_t remaining =
            MAX_RESPONSE_SIZE -
            check.response.size();

        const ssize_t received =
            recv(
                check.fd,
                buffer,
                std::min(
                    sizeof(buffer),
                    remaining
                ),
                0
            );

        if(received > 0) {

            check.response.append(
                buffer,
                static_cast<std::size_t>(
                    received
                )
            );

            if(
                check.response.find(
                    "HTTP/1.1 200 OK"
                ) != std::string::npos
            ) {

                finishCheck(
                    check.fd,
                    true,
                    epoll
                );

                return;
            }

            continue;
        }

        if(received == 0) {

            finishCheck(
                check.fd,
                false,
                epoll
            );

            return;
        }

        if(errno == EINTR) {
            continue;
        }

        if(
            errno == EAGAIN ||
            errno == EWOULDBLOCK
        ) {
            return;
        }

        failCheck(
            check.fd,
            epoll
        );

        return;
    }

    failCheck(
        check.fd,
        epoll
    );
}

void HealthChecker::finishCheck(
    int fd,
    bool healthy,
    Epoll& epoll
) {
    auto it = checks.find(fd);

    if(it == checks.end()) {
        return;
    }

    Check& check = it->second;

    if(
        backend_pool != nullptr &&
        check.backend_index <
        backend_pool->size()
    ) {

        Backend& backend =
            backend_pool->getBackend(
                check.backend_index
            );

        if(healthy) {

            backend.healthy = true;
            backend.consecutive_failures = 0;

        } else {

            backend.consecutive_failures++;

            if(
                backend.consecutive_failures >= 2
            ) {
                backend.healthy = false;
            }
        }
    }

    epoll.remove(fd);

    close(fd);

    checks.erase(it);
}

void HealthChecker::failCheck(
    int fd,
    Epoll& epoll
) {
    auto it = checks.find(fd);

    if(it == checks.end()) {
        return;
    }

    Check& check = it->second;

    if(
        backend_pool != nullptr &&
        check.backend_index <
        backend_pool->size()
    ) {

        Backend& backend =
            backend_pool->getBackend(
                check.backend_index
            );

        backend.consecutive_failures++;

        if(
            backend.consecutive_failures >= 2
        ) {
            backend.healthy = false;
        }
    }

    epoll.remove(fd);

    close(fd);

    checks.erase(it);
}