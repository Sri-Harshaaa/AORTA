#pragma once

#include "worker/WorkerPool.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

struct Task {
    std::size_t id{0};
    std::string title;
    bool completed{false};
};

class TaskManager {

private:
    static constexpr const char* TASK_SET_KEY =
        "tasks";

    static constexpr const char* NEXT_ID_KEY =
        "task:next_id";

    WorkerPool& worker_pool;
    std::size_t reactor_id{0};

    static std::string taskKey(
        std::size_t id
    );

public:
    using GetAllCallback =
        std::function<void(
            bool success,
            const std::vector<Task>& tasks
        )>;

    using CreateCallback =
        std::function<void(
            bool success,
            const Task& task
        )>;

    using UpdateCallback =
        std::function<void(
            bool success,
            const Task& task
        )>;

    using RemoveCallback =
        std::function<void(
            bool success,
            bool removed
        )>;

    TaskManager(
        WorkerPool& worker_pool,
        std::size_t reactor_id
    );

    bool getAll(
        GetAllCallback callback
    );

    bool create(
        const std::string& title,
        CreateCallback callback
    );

    bool update(
        std::size_t id,
        const std::string& title,
        bool completed,
        UpdateCallback callback
    );

    bool remove(
        std::size_t id,
        RemoveCallback callback
    );
};
