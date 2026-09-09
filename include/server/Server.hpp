#pragma once

#include "server/Reactor.hpp"
#include "server/Metrics.hpp"
#include "worker/WorkerPool.hpp"

#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

class Server {

private:
    int reactor_count{1};
    int port{8080};

    Metrics metrics;
    std::unique_ptr<WorkerPool> worker_pool;

    std::vector<std::unique_ptr<Reactor>> reactors;
    std::vector<std::thread> threads;

public:
    explicit Server(int port = 8080);

    void start();
};
