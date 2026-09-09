#include "redis/RedisClient.hpp"

#include <cstring>

#include <sys/time.h>

RedisClient::RedisClient(
    const std::string& redis_host,
    int redis_port
)
    : host(redis_host),
      port(redis_port) {
}


RedisClient::~RedisClient() {

    if(context != nullptr) {
        redisFree(context);
        context = nullptr;
    }
}


bool RedisClient::connect() {
    if(context != nullptr) {
        return true;
    }

    const struct timeval connect_timeout = {
        0,
        500000
    };

    context = redisConnectWithTimeout(
        host.c_str(),
        port,
        connect_timeout
    );

    if(context == nullptr) {
        return false;
    }

    if(context->err != 0) {
        redisFree(context);
        context = nullptr;
        return false;
    }

    const struct timeval command_timeout = {
        1,
        0
    };

    if(redisSetTimeout(context, command_timeout) != REDIS_OK) {
        redisFree(context);
        context = nullptr;
        return false;
    }

    return true;
}


bool RedisClient::ensureConnected() {
    if(
        context != nullptr &&
        context->err == 0
    ) {
        return true;
    }

    if(context != nullptr) {
        redisFree(context);
        context = nullptr;
    }

    return connect();
}


bool RedisClient::set(
    const std::string& key,
    const std::string& value
) {

    if(!ensureConnected()) {
        return false;
    }

    redisReply* reply =
        static_cast<redisReply*>(
            redisCommand(
                context,
                "SET %b %b",
                key.data(),
                key.size(),
                value.data(),
                value.size()
            )
        );

    if(reply == nullptr) {
        return false;
    }

    const bool success =
        reply->type == REDIS_REPLY_STATUS &&
        std::string(
            reply->str,
            reply->len
        ) == "OK";

    freeReplyObject(reply);

    return success;
}


bool RedisClient::get(
    const std::string& key,
    std::string& value
) {

    if(!ensureConnected()) {
        return false;
    }

    redisReply* reply =
        static_cast<redisReply*>(
            redisCommand(
                context,
                "GET %b",
                key.data(),
                key.size()
            )
        );

    if(reply == nullptr) {
        return false;
    }

    const bool success =
        reply->type == REDIS_REPLY_STRING;

    if(success) {
        value.assign(
            reply->str,
            reply->len
        );
    }

    freeReplyObject(reply);

    return success;
}


bool RedisClient::exists(
    const std::string& key
) {

    if(!ensureConnected()) {
        return false;
    }

    redisReply* reply =
        static_cast<redisReply*>(
            redisCommand(
                context,
                "EXISTS %b",
                key.data(),
                key.size()
            )
        );

    if(reply == nullptr) {
        return false;
    }

    const bool result =
        reply->type == REDIS_REPLY_INTEGER &&
        reply->integer == 1;

    freeReplyObject(reply);

    return result;
}


bool RedisClient::del(
    const std::string& key
) {

    if(!ensureConnected()) {
        return false;
    }

    redisReply* reply =
        static_cast<redisReply*>(
            redisCommand(
                context,
                "DEL %b",
                key.data(),
                key.size()
            )
        );

    if(reply == nullptr) {
        return false;
    }

    const bool success =
        reply->type == REDIS_REPLY_INTEGER;

    freeReplyObject(reply);

    return success;
}


bool RedisClient::incr(
    const std::string& key,
    std::size_t& value
) {

    if(!ensureConnected()) {
        return false;
    }

    redisReply* reply =
        static_cast<redisReply*>(
            redisCommand(
                context,
                "INCR %b",
                key.data(),
                key.size()
            )
        );

    if(reply == nullptr) {
        return false;
    }

    const bool success =
        reply->type == REDIS_REPLY_INTEGER;

    if(success) {
        value =
            static_cast<std::size_t>(
                reply->integer
            );
    }

    freeReplyObject(reply);

    return success;
}


bool RedisClient::hset(
    const std::string& key,
    const std::string& field,
    const std::string& value
) {

    if(!ensureConnected()) {
        return false;
    }

    redisReply* reply =
        static_cast<redisReply*>(
            redisCommand(
                context,
                "HSET %b %b %b",
                key.data(),
                key.size(),
                field.data(),
                field.size(),
                value.data(),
                value.size()
            )
        );

    if(reply == nullptr) {
        return false;
    }

    const bool success =
        reply->type == REDIS_REPLY_INTEGER;

    freeReplyObject(reply);

    return success;
}


bool RedisClient::hgetall(
    const std::string& key,
    std::string& title,
    bool& completed
) {

    if(!ensureConnected()) {
        return false;
    }

    redisReply* reply =
        static_cast<redisReply*>(
            redisCommand(
                context,
                "HGETALL %b",
                key.data(),
                key.size()
            )
        );

    if(reply == nullptr) {
        return false;
    }

    if(
        reply->type != REDIS_REPLY_ARRAY ||
        reply->elements % 2 != 0
    ) {
        freeReplyObject(reply);
        return false;
    }

    bool found_title = false;
    bool found_completed = false;

    for(
        std::size_t i = 0;
        i < reply->elements;
        i += 2
    ) {
        redisReply* field =
            reply->element[i];

        redisReply* value =
            reply->element[i + 1];

        if(
            field == nullptr ||
            value == nullptr ||
            field->type != REDIS_REPLY_STRING ||
            value->type != REDIS_REPLY_STRING
        ) {
            continue;
        }

        std::string field_name(
            field->str,
            field->len
        );

        if(field_name == "title") {
            title.assign(
                value->str,
                value->len
            );

            found_title = true;
        }

        if(field_name == "completed") {
            completed =
                std::string(
                    value->str,
                    value->len
                ) == "1";

            found_completed = true;
        }
    }

    freeReplyObject(reply);

    return found_title && found_completed;
}


bool RedisClient::sadd(
    const std::string& key,
    const std::string& value
) {

    if(!ensureConnected()) {
        return false;
    }

    redisReply* reply =
        static_cast<redisReply*>(
            redisCommand(
                context,
                "SADD %b %b",
                key.data(),
                key.size(),
                value.data(),
                value.size()
            )
        );

    if(reply == nullptr) {
        return false;
    }

    const bool success =
        reply->type == REDIS_REPLY_INTEGER;

    freeReplyObject(reply);

    return success;
}


bool RedisClient::srem(
    const std::string& key,
    const std::string& value
) {

    if(!ensureConnected()) {
        return false;
    }

    redisReply* reply =
        static_cast<redisReply*>(
            redisCommand(
                context,
                "SREM %b %b",
                key.data(),
                key.size(),
                value.data(),
                value.size()
            )
        );

    if(reply == nullptr) {
        return false;
    }

    const bool success =
        reply->type == REDIS_REPLY_INTEGER;

    freeReplyObject(reply);

    return success;
}


bool RedisClient::smembers(
    const std::string& key,
    std::vector<std::string>& values
) {

    if(!ensureConnected()) {
        return false;
    }

    redisReply* reply =
        static_cast<redisReply*>(
            redisCommand(
                context,
                "SMEMBERS %b",
                key.data(),
                key.size()
            )
        );

    if(reply == nullptr) {
        return false;
    }

    if(reply->type != REDIS_REPLY_ARRAY) {
        freeReplyObject(reply);
        return false;
    }

    values.clear();

    for(
        std::size_t i = 0;
        i < reply->elements;
        ++i
    ) {
        redisReply* element =
            reply->element[i];

        if(
            element != nullptr &&
            element->type == REDIS_REPLY_STRING
        ) {
            values.emplace_back(
                element->str,
                element->len
            );
        }
    }

    freeReplyObject(reply);

    return true;
}

bool RedisClient::getAllTasks(
    const std::string& task_set_key,
    std::vector<RedisTask>& tasks
) {
    if(!ensureConnected()) {
        return false;
    }

    static const char* SCRIPT = R"lua(
local ids = redis.call('SMEMBERS', KEYS[1])
local result = {}

for _, id in ipairs(ids) do
    local task_key = 'task:' .. id
    local fields = redis.call('HGETALL', task_key)

    local title = nil
    local completed = nil

    for i = 1, #fields, 2 do
        local field = fields[i]
        local value = fields[i + 1]

        if field == 'title' then
            title = value
        elseif field == 'completed' then
            completed = value
        end
    end

    if title ~= nil and completed ~= nil then
        table.insert(result, id)
        table.insert(result, title)
        table.insert(result, completed)
    end
end

return result
)lua";

    redisReply* reply =
        static_cast<redisReply*>(
            redisCommand(
                context,
                "EVAL %b 1 %b",
                SCRIPT,
                std::strlen(SCRIPT),
                task_set_key.data(),
                task_set_key.size()
            )
        );

    if(reply == nullptr) {
        return false;
    }

    if(reply->type != REDIS_REPLY_ARRAY) {
        freeReplyObject(reply);
        return false;
    }

    if(reply->elements % 3 != 0) {
        freeReplyObject(reply);
        return false;
    }

    std::vector<RedisTask> result;
    result.reserve(reply->elements / 3);

    for(
        std::size_t i = 0;
        i < reply->elements;
        i += 3
    ) {
        redisReply* id_reply =
            reply->element[i];

        redisReply* title_reply =
            reply->element[i + 1];

        redisReply* completed_reply =
            reply->element[i + 2];

        if(
            id_reply == nullptr ||
            title_reply == nullptr ||
            completed_reply == nullptr
        ) {
            continue;
        }

        if(
            id_reply->type != REDIS_REPLY_STRING ||
            title_reply->type != REDIS_REPLY_STRING ||
            completed_reply->type != REDIS_REPLY_STRING
        ) {
            continue;
        }

        std::size_t id{0};

        try {
            id = std::stoull(
                std::string(
                    id_reply->str,
                    id_reply->len
                )
            );
        } catch(...) {
            continue;
        }

        RedisTask task;

        task.id = id;

        task.title.assign(
            title_reply->str,
            title_reply->len
        );

        task.completed =
            std::string(
                completed_reply->str,
                completed_reply->len
            ) == "1";

        result.push_back(
            std::move(task)
        );
    }

    freeReplyObject(reply);

    tasks = std::move(result);

    return true;
}

bool RedisClient::createTask(
    const std::string& next_id_key,
    const std::string& task_set_key,
    const std::string& title,
    std::size_t& id
) {

    if(!ensureConnected()) {
        return false;
    }

    static const char* SCRIPT = R"lua(
local id = redis.call('INCR', KEYS[1])
local task_key = 'task:' .. id

redis.call(
    'HSET',
    task_key,
    'title',
    ARGV[1],
    'completed',
    '0'
)

redis.call(
    'SADD',
    KEYS[2],
    tostring(id)
)

return id
)lua";

    redisReply* reply =
        static_cast<redisReply*>(
            redisCommand(
                context,
                "EVAL %b 2 %b %b %b",
                SCRIPT,
                std::strlen(SCRIPT),
                next_id_key.data(),
                next_id_key.size(),
                task_set_key.data(),
                task_set_key.size(),
                title.data(),
                title.size()
            )
        );

    if(reply == nullptr) {
        return false;
    }

    const bool success =
        reply->type == REDIS_REPLY_INTEGER;

    if(success) {
        id =
            static_cast<std::size_t>(
                reply->integer
            );
    }

    freeReplyObject(reply);

    return success;
}


bool RedisClient::updateTask(
    const std::string& task_key,
    const std::string& title,
    bool completed,
    std::string& updated_title
) {

    if(!ensureConnected()) {
        return false;
    }

    static const char* SCRIPT = R"lua(
if redis.call('EXISTS', KEYS[1]) == 0 then
    return {0}
end

local current_title =
    redis.call('HGET', KEYS[1], 'title')

if ARGV[1] ~= '' then
    current_title = ARGV[1]
end

redis.call(
    'HSET',
    KEYS[1],
    'title',
    current_title,
    'completed',
    ARGV[2]
)

return {1, current_title}
)lua";

    const std::string completed_value =
        completed ? "1" : "0";

    redisReply* reply =
        static_cast<redisReply*>(
            redisCommand(
                context,
                "EVAL %b 1 %b %b %b",
                SCRIPT,
                std::strlen(SCRIPT),
                task_key.data(),
                task_key.size(),
                title.data(),
                title.size(),
                completed_value.data(),
                completed_value.size()
            )
        );

    if(reply == nullptr) {
        return false;
    }

    if(
        reply->type != REDIS_REPLY_ARRAY ||
        reply->elements != 2
    ) {
        freeReplyObject(reply);
        return false;
    }

    redisReply* status =
        reply->element[0];

    redisReply* result_title =
        reply->element[1];

    if(
        status == nullptr ||
        status->type != REDIS_REPLY_INTEGER ||
        status->integer != 1
    ) {
        freeReplyObject(reply);
        return false;
    }

    if(
        result_title == nullptr ||
        result_title->type != REDIS_REPLY_STRING
    ) {
        freeReplyObject(reply);
        return false;
    }

    updated_title.assign(
        result_title->str,
        result_title->len
    );

    freeReplyObject(reply);

    return true;
}


bool RedisClient::removeTask(
    const std::string& task_key,
    const std::string& task_set_key,
    const std::string& id,
    bool& removed
) {

    if(!ensureConnected()) {
        return false;
    }

    static const char* SCRIPT = R"lua(
if redis.call('EXISTS', KEYS[1]) == 0 then
    return 0
end

redis.call('DEL', KEYS[1])
redis.call('SREM', KEYS[2], ARGV[1])

return 1
)lua";

    redisReply* reply =
        static_cast<redisReply*>(
            redisCommand(
                context,
                "EVAL %b 2 %b %b %b",
                SCRIPT,
                std::strlen(SCRIPT),
                task_key.data(),
                task_key.size(),
                task_set_key.data(),
                task_set_key.size(),
                id.data(),
                id.size()
            )
        );

    if(reply == nullptr) {
        return false;
    }

    if(reply->type != REDIS_REPLY_INTEGER) {
        freeReplyObject(reply);
        return false;
    }

    removed =
        reply->integer == 1;

    freeReplyObject(reply);

    return true;
}
