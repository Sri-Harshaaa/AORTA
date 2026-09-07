#pragma once

#include "lb/BackendPool.hpp"
#include "lb/BalancingStrategy.hpp"
#include "lb/HealthChecker.hpp"
#include "lb/LbConfig.hpp"
#include "lb/LbMetrics.hpp"
#include "net/Epoll.hpp"
#include "net/Socket.hpp"
#include "net/TimerFd.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

class LoadBalancer {

private:
    struct ConnectionPair {

        int client_fd{-1};

        int backend_fd{-1};

        bool backend_connecting{false};

        bool client_read_closed{false};

        bool backend_read_closed{false};

        bool closing{false};

        bool metrics_connection{false};

        std::size_t backend_index{0};

        std::string client_ip;

        std::string client_to_backend;

        std::string backend_to_client;

        std::size_t client_to_backend_offset{0};

        std::size_t backend_to_client_offset{0};
    };

    LbConfig config;

    std::unique_ptr<BalancingStrategy> strategy;

    BackendPool backend_pool;

    HealthChecker health_checker;

    Socket listen_socket;

    Epoll epoll;

    TimerFd health_timer;

    std::unordered_map<int, ConnectionPair> connections;

    std::unordered_map<int, int> backend_to_client;

    /*
     * Bytes currently sitting in relay buffers across every connection.
     * Maintained incrementally by forwardData, flushData and closeConnection.
     */
    std::size_t buffered_bytes{0};

    static std::atomic<bool> shutdown_requested;

    static void handleSignal(int signal);

    void handleAccept();

    void handleHealthCheck();

    void handleHealthEvent(
        int fd,
        uint32_t events
    );

    void forwardData(
        int source_fd,
        std::string& output_buffer,
        std::size_t& offset
    );

    void flushData(
        int destination_fd,
        std::string& output_buffer,
        std::size_t& offset
    );

    /*
     * True when a direction still has room to buffer. Used to gate EPOLLIN so
     * a congested peer stops us reading instead of growing memory without
     * bound - real backpressure rather than dropping the connection.
     */
    bool canRead(
        const std::string& output_buffer,
        std::size_t offset
    ) const;

    void closeConnection(
        int client_fd
    );

    void closeAllConnections();

    bool connectClientToBackend(
        int client_fd
    );

    int connectToBackend(
        const Backend& backend,
        bool& connecting
    );

public:
    explicit LoadBalancer(const LbConfig& config);

    void start();
};
