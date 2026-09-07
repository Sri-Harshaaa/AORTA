#pragma once

#include "net/Socket.hpp"
#include "net/Epoll.hpp"
#include "net/Connection.hpp"
#include "net/TimerFd.hpp"
#include "task/TaskManager.hpp"
#include "server/Metrics.hpp"
#include "http/HttpHandler.hpp"

#include <chrono>
#include <memory>
#include <unordered_map>

class Reactor {

private:
    static constexpr int TIMER_INTERVAL_SECONDS = 1;
    static constexpr int REQUEST_TIMEOUT_SECONDS = 10;
    static constexpr int KEEP_ALIVE_TIMEOUT_SECONDS = 30;

    struct ConnectionState {
        std::chrono::steady_clock::time_point deadline;
    };

    int id;

    std::shared_ptr<TaskManager> task_manager;

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

public:
    Reactor(
        int id,
        std::shared_ptr<TaskManager> task_manager
    );

    void run(int port);

    const Metrics& getMetrics() const;
};