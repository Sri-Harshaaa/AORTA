#pragma once

#include "server/MetricsRegistry.hpp"
#include "server/Reactor.hpp"
#include "server/ServerConfig.hpp"

#include <memory>
#include <thread>
#include <vector>

/*
 * The epoll server: one reactor per worker, each on its own thread, all
 * bound to the same port through SO_REUSEPORT.
 */
class Server {

private:
    ServerConfig config;

    std::shared_ptr<MetricsRegistry> registry;

    std::vector<std::unique_ptr<Reactor>> reactors;
    std::vector<std::thread> threads;

public:
    explicit Server(const ServerConfig& config);

    void start();
};
