#include "task/TaskManager.hpp"

#include <algorithm>

TaskManager::TaskManager()
    : redis("redis", 6379) {
}


std::string TaskManager::taskKey(
    std::size_t id
) {
    return "task:" + std::to_string(id);
}


std::vector<Task> TaskManager::getAll() {

    std::vector<std::string> ids;

    if(!redis.smembers(
        TASK_SET_KEY,
        ids
    )) {
        return {};
    }

    std::vector<Task> tasks;

    tasks.reserve(ids.size());

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

        tasks.push_back({
            id,
            title,
            completed
        });
    }

    std::sort(
        tasks.begin(),
        tasks.end(),
        [](const Task& first, const Task& second) {
            return first.id < second.id;
        }
    );

    return tasks;
}


bool TaskManager::create(
    const std::string& title,
    Task& created_task
) {
    if(title.empty()) {
        return false;
    }

    std::size_t id{0};

    if(!redis.createTask(
        NEXT_ID_KEY,
        TASK_SET_KEY,
        title,
        id
    )) {
        return false;
    }

    created_task = {
        id,
        title,
        false
    };

    return true;
}


bool TaskManager::update(
    std::size_t id,
    const std::string& title,
    bool completed,
    Task& updated_task
) {
    std::string updated_title;

    if(!redis.updateTask(
        taskKey(id),
        title,
        completed,
        updated_title
    )) {
        return false;
    }

    updated_task = {
        id,
        updated_title,
        completed
    };

    return true;
}


bool TaskManager::remove(
    std::size_t id
) {
    bool removed{false};

    if(!redis.removeTask(
        taskKey(id),
        TASK_SET_KEY,
        std::to_string(id),
        removed
    )) {
        return false;
    }

    return removed;
}