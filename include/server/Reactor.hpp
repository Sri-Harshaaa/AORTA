#pragma once

#include "net/Socket.hpp"
#include "net/Epoll.hpp"
#include "net/Connection.hpp"
#include "net/TimerFd.hpp"
#include "task/TaskManager.hpp"
#include "server/Metrics.hpp"
#include "http/HttpHandler.hpp"
#include "worker/WorkerPool.hpp"

#include <chrono>
#include <cstdint>
#include <unordered_map>

class Reactor {

private:
    static constexpr int TIMER_INTERVAL_SECONDS = 1;
    static constexpr int REQUEST_TIMEOUT_SECONDS = 10;
    static constexpr int KEEP_ALIVE_TIMEOUT_SECONDS = 30;

    struct ConnectionState {
        std::chrono::steady_clock::time_point deadline;
        std::chrono::steady_clock::time_point request_start;
        std::uint64_t generation{0};
        bool request_in_flight{false};
        bool redis_pending{false};
    };

    int id;
    WorkerPool& worker_pool;
    TaskManager task_manager;
    Metrics& metrics;
    HttpHandler http_handler;

    Socket listen_socket;
    Epoll epoll;
    TimerFd timer;

    int completion_fd{-1};
    std::uint64_t next_connection_generation{0};

    std::unordered_map<int, std::unique_ptr<Connection>> connections;
    std::unordered_map<int, ConnectionState> connection_states;

    void handleAccept();
    void handleClient(int fd);
    void handleWrite(int fd);
    void handleCompletions();
    void handleTimer();
    void handleEvent(struct epoll_event& event);

    void removeConnection(int fd);
    void updateEvents(int fd, uint32_t events);

    void refreshDeadline(int fd, int timeout_seconds);
    void removeExpiredConnections();

    void completeAsyncResponse(
        int fd,
        std::uint64_t generation,
        bool close_after_write,
        HttpResponse response
    );

    static bool isGenerationCurrent(
        const ConnectionState& state,
        std::uint64_t generation
    );

public:
    Reactor(
        int id,
        WorkerPool& worker_pool,
        Metrics& metrics
    );

    void run(int port);

    const Metrics& getMetrics() const;
};
