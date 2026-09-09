#pragma once

#include <hiredis/hiredis.h>

#include <cstddef>
#include <string>
#include <vector>

struct RedisTask {
    std::size_t id{0};
    std::string title;
    bool completed{false};
};

class RedisClient {

private:
    std::string host;
    int port{6379};

    redisContext* context{nullptr};

    bool connect();
    bool ensureConnected();

public:
    RedisClient(
        const std::string& host,
        int port
    );

    ~RedisClient();

    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    bool set(
        const std::string& key,
        const std::string& value
    );

    bool get(
        const std::string& key,
        std::string& value
    );

    bool exists(
        const std::string& key
    );

    bool del(
        const std::string& key
    );

    bool incr(
        const std::string& key,
        std::size_t& value
    );

    bool hset(
        const std::string& key,
        const std::string& field,
        const std::string& value
    );

    bool hgetall(
        const std::string& key,
        std::string& title,
        bool& completed
    );

    bool sadd(
        const std::string& key,
        const std::string& value
    );

    bool srem(
        const std::string& key,
        const std::string& value
    );

    bool smembers(
        const std::string& key,
        std::vector<std::string>& values
    );

    bool getAllTasks(
        const std::string& task_set_key,
        std::vector<RedisTask>& tasks
    );

    bool createTask(
        const std::string& next_id_key,
        const std::string& task_set_key,
        const std::string& title,
        std::size_t& id
    );

    bool updateTask(
        const std::string& task_key,
        const std::string& title,
        bool completed,
        std::string& updated_title
    );

    bool removeTask(
        const std::string& task_key,
        const std::string& task_set_key,
        const std::string& id,
        bool& removed
    );
};