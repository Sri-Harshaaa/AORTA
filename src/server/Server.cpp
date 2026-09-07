#include "server/Server.hpp"

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

    reactor_count = std::thread::hardware_concurrency();

    if(reactor_count == 0) {
        reactor_count = 1;
    }

    task_manager = std::make_shared<TaskManager>();

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::cout << "Creating " << reactor_count
              << " reactors on port "
              << port
              << std::endl;
}


void Server::start() {
    for(int i = 0; i < reactor_count; i++) {
        reactors.push_back(
            std::make_unique<Reactor>(
                i,
                task_manager
            )
        );
    }

    for(int i = 0; i < reactor_count; i++) {
        threads.emplace_back([this, i]() {
            reactors[i]->run(port);
        });
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

    std::cout << "All reactors stopped" << std::endl;
}