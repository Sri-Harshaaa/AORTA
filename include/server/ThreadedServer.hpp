#pragma once

#include "http/HttpHandler.hpp"
#include "net/Socket.hpp"
#include "server/Metrics.hpp"
#include "server/MetricsRegistry.hpp"
#include "server/ServerConfig.hpp"
#include "task/TaskStore.hpp"

#include <atomic>
#include <memory>
#include <string>

/*
 * The thread-per-connection server.
 *
 * This exists to be the baseline the epoll server is measured against, so it
 * is deliberately the textbook design: a blocking accept loop, one OS thread
 * per connection, blocking reads and writes inside that thread. It reuses
 * HttpParser, HttpHandler and HttpResponse unchanged, so a comparison between
 * the two models isolates the concurrency model rather than the HTTP code.
 *
 * Threads are detached and capped. Past the cap the server still accepts and
 * then immediately refuses with 503, which turns "we ran out of thread stacks"
 * from a crash into a measurable rejection count.
 */
class ThreadedServer {

public:
    ThreadedServer(
        const ServerConfig& config,
        std::shared_ptr<MetricsRegistry> registry
    );

    void start();

private:
    static constexpr int RECEIVE_TIMEOUT_SECONDS = 30;
    static constexpr int SEND_TIMEOUT_SECONDS = 10;
    static constexpr int ACCEPT_TIMEOUT_SECONDS = 1;

    ServerConfig config;

    std::shared_ptr<MetricsRegistry> registry;
    std::shared_ptr<TaskStore> task_store;

    Metrics metrics;

    HttpHandler http_handler;

    Socket listen_socket;

    std::atomic<int> active_threads{0};

    void handleConnection(int client_fd);

    bool sendAll(int fd, const char* data, std::size_t size);

    void refuse(int client_fd);
};
