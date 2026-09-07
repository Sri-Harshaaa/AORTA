#pragma once

#include "task/TaskManager.hpp"
#include "task/TaskStore.hpp"

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/*
 * A bounded pool of Redis connections, for the thread-per-connection server.
 *
 * That model can have thousands of threads live at once, and opening a Redis
 * connection per thread would exhaust Redis long before the server hit its own
 * limits. Threads check a connection out for the duration of one call and wait
 * if none is free, which is the correct trade for a model whose threads are
 * expected to block anyway.
 *
 * The epoll server must never use this: waiting for a free connection inside
 * an event loop would stall every other connection that reactor holds. It uses
 * one TaskManager per reactor instead.
 */
class PooledTaskStore : public TaskStore {

public:
    PooledTaskStore(
        const std::string& redis_host,
        int redis_port,
        std::size_t pool_size
    );

    std::vector<Task> getAll() override;

    bool create(
        const std::string& title,
        Task& created_task
    ) override;

    bool update(
        std::size_t id,
        const std::string& title,
        bool completed,
        Task& updated_task
    ) override;

    bool remove(
        std::size_t id
    ) override;

    std::size_t size() const;

private:
    /*
     * RAII checkout. Returns the connection to the free list even if the
     * operation below it throws.
     */
    class Lease {

    public:
        explicit Lease(PooledTaskStore& owner);
        ~Lease();

        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;

        TaskManager& get();

    private:
        PooledTaskStore& owner;
        std::size_t index;
    };

    std::vector<std::unique_ptr<TaskManager>> clients;
    std::vector<std::size_t> free_list;

    mutable std::mutex mutex;
    std::condition_variable available;

    std::size_t acquire();
    void release(std::size_t index);
};
