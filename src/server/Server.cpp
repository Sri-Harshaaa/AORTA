#include "server/Server.hpp"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

volatile std::sig_atomic_t shutdown_requested = 0;

namespace {

void handleSignal(int signal) {
    if(signal == SIGINT || signal == SIGTERM) {
        shutdown_requested = 1;
    }
}

}


Server::Server(int port)
    : port(port) {
    const unsigned int hardware =
        std::thread::hardware_concurrency();

    reactor_count =
        hardware == 0
            ? 1
            : static_cast<int>(hardware);

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    worker_pool =
        std::make_unique<WorkerPool>(
            static_cast<std::size_t>(reactor_count)
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
                *worker_pool
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
