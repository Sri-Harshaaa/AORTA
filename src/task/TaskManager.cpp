#include "task/TaskManager.hpp"

#include <algorithm>
#include <memory>

TaskManager::TaskManager(
    WorkerPool& worker_pool,
    std::size_t reactor_id
)
    : worker_pool(worker_pool),
      reactor_id(reactor_id) {
}


std::string TaskManager::taskKey(
    std::size_t id
) {
    return "task:" + std::to_string(id);
}


bool TaskManager::getAll(
    GetAllCallback callback
) {
    if(!callback) {
        return false;
    }

    auto result =
        std::make_shared<std::vector<Task>>();

    auto success =
        std::make_shared<bool>(false);

    return worker_pool.submit(
        reactor_id,
        [callback = std::move(callback), result, success](RedisClient& redis) mutable {
            std::vector<RedisTask> redis_tasks;

            *success =
                redis.getAllTasks(
                    TASK_SET_KEY,
                    redis_tasks
                );

            if(*success) {
                result->reserve(
                    redis_tasks.size()
                );

                for(const RedisTask& redis_task : redis_tasks) {
                    result->push_back({
                        redis_task.id,
                        redis_task.title,
                        redis_task.completed
                    });
                }

                std::sort(
                    result->begin(),
                    result->end(),
                    [](const Task& first, const Task& second) {
                        return first.id < second.id;
                    }
                );
            }

            return [
                callback = std::move(callback),
                result,
                success
            ]() mutable {
                callback(
                    *success,
                    *result
                );
            };
        }
    );
}

bool TaskManager::create(
    const std::string& title,
    CreateCallback callback
) {
    if(!callback || title.empty()) {
        return false;
    }

    auto result =
        std::make_shared<Task>();

    auto success =
        std::make_shared<bool>(false);

    return worker_pool.submit(
        reactor_id,
        [title, callback = std::move(callback), result, success](RedisClient& redis) mutable {
            std::size_t id{0};

            *success =
                redis.createTask(
                    NEXT_ID_KEY,
                    TASK_SET_KEY,
                    title,
                    id
                );

            if(*success) {
                *result = {
                    id,
                    title,
                    false
                };
            }

            return [callback = std::move(callback), result, success]() mutable {
                callback(
                    *success,
                    *result
                );
            };
        }
    );
}


bool TaskManager::update(
    std::size_t id,
    const std::string& title,
    bool completed,
    UpdateCallback callback
) {
    if(!callback) {
        return false;
    }

    auto result =
        std::make_shared<Task>();

    auto success =
        std::make_shared<bool>(false);

    return worker_pool.submit(
        reactor_id,
        [id, title, completed, callback = std::move(callback), result, success](RedisClient& redis) mutable {
            std::string updated_title;

            *success =
                redis.updateTask(
                    taskKey(id),
                    title,
                    completed,
                    updated_title
                );

            if(*success) {
                *result = {
                    id,
                    updated_title,
                    completed
                };
            }

            return [callback = std::move(callback), result, success]() mutable {
                callback(
                    *success,
                    *result
                );
            };
        }
    );
}


bool TaskManager::remove(
    std::size_t id,
    RemoveCallback callback
) {
    if(!callback) {
        return false;
    }

    auto removed =
        std::make_shared<bool>(false);

    auto success =
        std::make_shared<bool>(false);

    return worker_pool.submit(
        reactor_id,
        [id, callback = std::move(callback), removed, success](RedisClient& redis) mutable {
            *success =
                redis.removeTask(
                    taskKey(id),
                    TASK_SET_KEY,
                    std::to_string(id),
                    *removed
                );

            return [callback = std::move(callback), removed, success]() mutable {
                callback(
                    *success,
                    *removed
                );
            };
        }
    );
}
