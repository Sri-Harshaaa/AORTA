#pragma once

#include "server/Reactor.hpp"
#include "task/TaskManager.hpp"

#include <memory>
#include <thread>
#include <vector>

class Server {

private:
    int reactor_count;
    int port;

    std::shared_ptr<TaskManager> task_manager;

    std::vector<std::unique_ptr<Reactor>> reactors;
    std::vector<std::thread> threads;

public:
    explicit Server(int port = 8080);

    void start();
};