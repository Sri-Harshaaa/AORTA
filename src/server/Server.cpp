#include "server/Server.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

volatile std::sig_atomic_t shutdown_requested = 0;

namespace {

void handleSignal(int signal) {
    if(signal == SIGINT || signal == SIGTERM) {
        shutdown_requested = 1;
    }
}

std::size_t loadThreadCount(
    const char* name,
    std::size_t fallback
) {
    const char* value = std::getenv(name);

    if(value == nullptr) {
        return fallback;
    }

    try {
        const std::size_t count = std::stoull(value);
        return count == 0 ? fallback : count;
    } catch(...) {
        return fallback;
    }
}

}


Server::Server(int port)
    : port(port) {
    const unsigned int hardware =
        std::thread::hardware_concurrency();

    const std::size_t default_reactors =
        hardware == 0 ? 1 : hardware;

    reactor_count = static_cast<int>(
        loadThreadCount(
            "AORTA_REACTORS",
            default_reactors
        )
    );

    const std::size_t worker_count =
        loadThreadCount(
            "AORTA_WORKERS",
            default_reactors / 2
        );

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    worker_pool =
        std::make_unique<WorkerPool>(
            static_cast<std::size_t>(reactor_count),
            worker_count
        );

    std::cout
        << "Creating "
        << reactor_count
        << " reactors on port "
        << port
        << std::endl;
}


void Server::start() {
    reactors.reserve(
        static_cast<std::size_t>(reactor_count)
    );

    threads.reserve(
        static_cast<std::size_t>(reactor_count)
    );

    for(int i = 0; i < reactor_count; ++i) {
        reactors.push_back(
            std::make_unique<Reactor>(
                i,
                *worker_pool,
                metrics
            )
        );
    }

    for(int i = 0; i < reactor_count; ++i) {
        threads.emplace_back(
            [this, i]() {
                reactors[i]->run(port);
            }
        );
    }

    while(shutdown_requested == 0) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(100)
        );
    }

    std::cout << std::endl;
    std::cout << "Shutdown requested" << std::endl;

    for(auto& thread : threads) {
        if(thread.joinable()) {
            thread.join();
        }
    }

    worker_pool->stop();

    std::cout
        << "All reactors stopped"
        << std::endl;
}
