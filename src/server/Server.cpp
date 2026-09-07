#include "server/Server.hpp"
#include "server/Shutdown.hpp"

#include <chrono>
#include <iostream>
#include <thread>

Server::Server(const ServerConfig& server_config)
    : config(server_config),
      registry(std::make_shared<MetricsRegistry>()) {

    if(config.workers <= 0) {

        const unsigned int detected =
            std::thread::hardware_concurrency();

        config.workers =
            detected == 0 ? 1 : static_cast<int>(detected);
    }

    registry->setMode(ServerConfig::modeName(config.mode));

    shutdown::install();

    config.print();
}


void Server::start() {
    reactors.reserve(static_cast<std::size_t>(config.workers));

    /*
     * Every reactor is constructed before any thread starts, so each has
     * registered its counters with the registry by the time a scrape can
     * arrive.
     */
    for(int i = 0; i < config.workers; i++) {
        reactors.push_back(
            std::make_unique<Reactor>(
                i,
                config,
                registry
            )
        );
    }

    threads.reserve(static_cast<std::size_t>(config.workers));

    for(int i = 0; i < config.workers; i++) {
        threads.emplace_back([this, i]() {
            reactors[static_cast<std::size_t>(i)]->run();
        });
    }

    std::cout
        << "Listening on port "
        << config.port
        << " with "
        << config.workers
        << " reactors"
        << std::endl;

    while(!shutdown::isRequested()) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(100)
        );
    }

    std::cout << "\nShutdown requested" << std::endl;

    for(auto& thread : threads) {
        if(thread.joinable()) {
            thread.join();
        }
    }

    std::cout << "All reactors stopped" << std::endl;
}
