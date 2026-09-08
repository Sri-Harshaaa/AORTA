#pragma once

#include "redis/RedisClient.hpp"

#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class WorkerPool {

public:
    using Completion = std::function<void()>;
    using Work = std::function<Completion(RedisClient&)>;

private:
    struct Job {
        std::size_t reactor_id{0};
        Work work;
    };

    struct CompletionChannel {
        int event_fd{-1};
        std::mutex mutex;
        std::deque<Completion> completions;
    };

    std::mutex jobs_mutex;
    std::condition_variable jobs_condition;
    std::deque<Job> jobs;

    std::vector<std::thread> workers;
    std::vector<std::unique_ptr<CompletionChannel>> channels;

    static constexpr std::size_t MAX_QUEUED_JOBS = 4096;

    bool stopping{false};

    void workerLoop();

    void publishCompletion(
        std::size_t reactor_id,
        Completion completion
    );

public:
    explicit WorkerPool(
        std::size_t reactor_count,
        std::size_t worker_count = 0
    );

    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    bool submit(
        std::size_t reactor_id,
        Work work
    );

    int getCompletionFd(
        std::size_t reactor_id
    ) const;

    void consumeCompletionEvent(
        std::size_t reactor_id
    );

    bool popCompletion(
        std::size_t reactor_id,
        Completion& completion
    );

    std::size_t getWorkerCount() const;

    void stop();
};
