#pragma once

#include "lb/BackendPool.hpp"
#include "net/Epoll.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class HealthChecker {

private:
    static constexpr int CONNECT_TIMEOUT_MS = 500;
    static constexpr int RESPONSE_TIMEOUT_MS = 500;
    static constexpr std::size_t MAX_RESPONSE_SIZE = 4096;

    enum class State {
        Connecting,
        Writing,
        Reading
    };

    struct Check {

        int fd{-1};

        std::size_t backend_index{0};

        State state{State::Connecting};

        std::string request;

        std::size_t write_offset{0};

        std::string response;

        std::chrono::steady_clock::time_point deadline;
    };

    std::unordered_map<int, Check> checks;

    BackendPool* backend_pool{nullptr};

    void finishCheck(
        int fd,
        bool healthy,
        Epoll& epoll
    );

    void failCheck(
        int fd,
        Epoll& epoll
    );

    void handleConnect(
        Check& check,
        Epoll& epoll
    );

    void handleWrite(
        Check& check,
        Epoll& epoll
    );

    void handleRead(
        Check& check,
        Epoll& epoll
    );

public:
    HealthChecker() = default;

    ~HealthChecker();

    HealthChecker(const HealthChecker&) = delete;
    HealthChecker& operator=(const HealthChecker&) = delete;

    void start(
        BackendPool& pool,
        Epoll& epoll
    );

    void handleEvent(
        int fd,
        uint32_t events,
        Epoll& epoll
    );

    bool handles(
        int fd
    ) const;

    bool active() const;

    int nextTimeoutMs() const;

    void expire(
        Epoll& epoll
    );

    void cancelAll(
        Epoll& epoll
    );
};