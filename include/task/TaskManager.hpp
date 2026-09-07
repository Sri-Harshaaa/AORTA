#pragma once

#include "redis/RedisClient.hpp"
#include "task/TaskStore.hpp"

#include <cstddef>
#include <string>
#include <vector>

/*
 * A task store backed by exactly one Redis connection.
 *
 * One of these per reactor means a reactor never blocks behind another
 * reactor's Redis round trip. The connection is still synchronous, so a call
 * does park the calling thread for the duration of the round trip - see the
 * note in README about why that is the remaining bottleneck.
 */
class TaskManager : public TaskStore {

private:
    static constexpr const char* TASK_SET_KEY = "tasks";
    static constexpr const char* NEXT_ID_KEY = "task:next_id";

    RedisClient redis;

    static std::string taskKey(std::size_t id);

public:
    TaskManager(
        const std::string& redis_host,
        int redis_port
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
};
