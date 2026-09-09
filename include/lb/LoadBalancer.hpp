#pragma once

#include "lb/BackendPool.hpp"
#include "lb/ConsistentHash.hpp"
#include "lb/HealthChecker.hpp"
#include "lb/LbMetrics.hpp"
#include "net/Epoll.hpp"
#include "net/Socket.hpp"
#include "net/TimerFd.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

class LoadBalancer {

private:
    static constexpr int LISTEN_PORT = 9000;
    static constexpr int HEALTH_CHECK_INTERVAL = 5;

    static constexpr std::size_t MAX_BUFFER_SIZE =
        8 * 1024 * 1024;

    static constexpr std::size_t MAX_CONNECTIONS =
        100000;

    enum class RoutingMode {
        RoundRobin,
        ConsistentHash
    };

    struct ConnectionPair {

        int client_fd{-1};

        int backend_fd{-1};

        uint32_t current_client_events{0};

        uint32_t current_backend_events{0};

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

    int reactor_id{0};
    int reactor_count{1};

    RoutingMode routing_mode{RoutingMode::RoundRobin};

    std::size_t next_backend_index{0};

    BackendPool backend_pool;

    ConsistentHash consistent_hash;

    HealthChecker health_checker;

    Socket listen_socket;

    Epoll epoll;

    TimerFd health_timer;

    std::unordered_map<int, ConnectionPair> connections;

    std::unordered_map<int, int> backend_to_client;

    static std::atomic<bool> shutdown_requested;

    static void handleSignal(int signal);

    void handleAccept();

    void handleHealthCheck();

    void handleHealthEvent(
        int fd,
        uint32_t events
    );

    bool handleMetricsRequest(
        int client_fd,
        ConnectionPair& connection
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

    void updateWriteInterest(
        int fd,
        bool enabled
    );

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

    std::size_t selectRoundRobinBackend();

    std::optional<std::size_t> selectBackend(
        const std::string& client_ip
    );

    static RoutingMode loadRoutingMode();

public:
    LoadBalancer(
        int reactor_id,
        int reactor_count
    );

    void start();
};