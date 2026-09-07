#pragma once

#include "redis/RedisClient.hpp"

#include <cstddef>
#include <string>
#include <vector>

struct Task {
    std::size_t id;
    std::string title;
    bool completed;
};

class TaskManager {

private:
    static constexpr const char* TASK_SET_KEY =
        "tasks";

    static constexpr const char* NEXT_ID_KEY =
        "task:next_id";

    RedisClient redis;

    static std::string taskKey(
        std::size_t id
    );

public:
    TaskManager();

    std::vector<Task> getAll();

    bool create(
        const std::string& title,
        Task& created_task
    );

    bool update(
        std::size_t id,
        const std::string& title,
        bool completed,
        Task& updated_task
    );

    bool remove(
        std::size_t id
    );
};