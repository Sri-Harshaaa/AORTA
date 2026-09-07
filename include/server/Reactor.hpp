#pragma once

#include "net/Socket.hpp"
#include "net/Epoll.hpp"
#include "net/Connection.hpp"
#include "net/TimerFd.hpp"
#include "http/HttpHandler.hpp"
#include "server/Metrics.hpp"
#include "server/MetricsRegistry.hpp"
#include "server/ServerConfig.hpp"
#include "task/TaskStore.hpp"

#include <chrono>
#include <memory>
#include <unordered_map>

/*
 * One event loop, one listening socket, one thread.
 *
 * Every reactor binds the same port with SO_REUSEPORT and lets the kernel
 * spread accepts across them. Each owns its own Redis connection so no reactor
 * ever waits behind another's round trip.
 */
class Reactor {

private:
    static constexpr int TIMER_INTERVAL_SECONDS = 1;
    static constexpr int REQUEST_TIMEOUT_SECONDS = 10;
    static constexpr int KEEP_ALIVE_TIMEOUT_SECONDS = 30;

    struct ConnectionState {
        std::chrono::steady_clock::time_point deadline;
    };

    int id;

    ServerConfig config;

    std::shared_ptr<TaskStore> task_store;

    Metrics metrics;

    HttpHandler http_handler;

    Socket listen_socket;
    Epoll epoll;
    TimerFd timer;

    std::unordered_map<int, std::unique_ptr<Connection>> connections;
    std::unordered_map<int, ConnectionState> connection_states;

    void handleAccept();
    void handleClient(int fd);
    void handleWrite(int fd);
    void handleTimer();
    void handleEvent(struct epoll_event& event);
    void removeConnection(int fd);
    void updateEvents(int fd, uint32_t events);

    void refreshDeadline(int fd, int timeout_seconds);
    void removeExpiredConnections();

    void drainCounters(Connection& connection);

public:
    Reactor(
        int id,
        const ServerConfig& config,
        std::shared_ptr<MetricsRegistry> registry
    );

    void run();

    const Metrics& getMetrics() const;
};
