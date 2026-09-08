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
            bool operation_success = true;

            std::vector<std::string> ids;

            if(!redis.smembers(
                TASK_SET_KEY,
                ids
            )) {
                operation_success = false;
            } else {
                result->reserve(ids.size());

                for(const std::string& id_string : ids) {
                    std::size_t id{0};

                    try {
                        id = std::stoull(id_string);
                    } catch(...) {
                        continue;
                    }

                    std::string title;
                    bool completed{false};

                    if(!redis.hgetall(
                        taskKey(id),
                        title,
                        completed
                    )) {
                        continue;
                    }

                    result->push_back({
                        id,
                        title,
                        completed
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

            *success = operation_success;

            return [callback = std::move(callback), result, success]() mutable {
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
