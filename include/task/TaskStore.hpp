#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct Task {
    std::size_t id{0};
    std::string title;
    bool completed{false};
};

/*
 * The storage operations the HTTP layer needs, decoupled from how the Redis
 * connections behind them are owned.
 *
 * The two concurrency models need different ownership. The epoll server gives
 * each reactor its own connection so no reactor ever waits on another. The
 * threaded server has far more threads than it should have connections, so it
 * shares a bounded pool. Both look the same from HttpHandler.
 */
class TaskStore {

public:
    virtual ~TaskStore() = default;

    virtual std::vector<Task> getAll() = 0;

    virtual bool create(
        const std::string& title,
        Task& created_task
    ) = 0;

    virtual bool update(
        std::size_t id,
        const std::string& title,
        bool completed,
        Task& updated_task
    ) = 0;

    virtual bool remove(
        std::size_t id
    ) = 0;
};
