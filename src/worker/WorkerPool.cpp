#include "worker/WorkerPool.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include <sys/eventfd.h>
#include <unistd.h>

WorkerPool::WorkerPool(
    std::size_t reactor_count,
    std::size_t worker_count
) {
    if(worker_count == 0) {
        const unsigned int hardware =
            std::thread::hardware_concurrency();

        worker_count =
            hardware == 0
                ? 1
                : std::max<std::size_t>(
                    1,
                    hardware / 2
                );
    }

    channels.reserve(reactor_count);

    for(std::size_t i = 0; i < reactor_count; ++i) {
        auto channel =
            std::make_unique<CompletionChannel>();

        channel->event_fd = eventfd(
            0,
            EFD_NONBLOCK | EFD_CLOEXEC
        );

        if(channel->event_fd == -1) {
            std::cerr
                << "eventfd() failed for reactor "
                << i
                << std::endl;

            for(auto& existing : channels) {
                if(existing->event_fd != -1) {
                    close(existing->event_fd);
                    existing->event_fd = -1;
                }
            }

            channels.clear();
            return;
        }

        channels.push_back(std::move(channel));
    }

    workers.reserve(worker_count);

    for(std::size_t i = 0; i < worker_count; ++i) {
        workers.emplace_back(
            [this]() {
                workerLoop();
            }
        );
    }

    std::cout
        << "Worker pool started with "
        << workers.size()
        << " workers"
        << std::endl;
}


WorkerPool::~WorkerPool() {
    stop();

    for(auto& channel : channels) {
        if(channel->event_fd != -1) {
            close(channel->event_fd);
            channel->event_fd = -1;
        }
    }
}


bool WorkerPool::submit(
    std::size_t reactor_id,
    Work work
) {
    if(!work) {
        return false;
    }

    if(reactor_id >= channels.size()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(jobs_mutex);

    if(
        stopping
        ||
        workers.empty()
        ||
        jobs.size() >= MAX_QUEUED_JOBS
    ) {
        return false;
    }

    jobs.push_back({
        reactor_id,
        std::move(work)
    });

    jobs_condition.notify_one();

    return true;
}


int WorkerPool::getCompletionFd(
    std::size_t reactor_id
) const {
    if(reactor_id >= channels.size()) {
        return -1;
    }

    return channels[reactor_id]->event_fd;
}


void WorkerPool::consumeCompletionEvent(
    std::size_t reactor_id
) {
    if(reactor_id >= channels.size()) {
        return;
    }

    const int fd = channels[reactor_id]->event_fd;

    if(fd == -1) {
        return;
    }

    while(true) {
        std::uint64_t value{0};

        const ssize_t result =
            read(
                fd,
                &value,
                sizeof(value)
            );

        if(result == static_cast<ssize_t>(sizeof(value))) {
            continue;
        }

        if(result == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }

        if(result == -1 && errno == EINTR) {
            continue;
        }

        break;
    }
}


bool WorkerPool::popCompletion(
    std::size_t reactor_id,
    Completion& completion
) {
    if(reactor_id >= channels.size()) {
        return false;
    }

    auto& channel = *channels[reactor_id];

    std::lock_guard<std::mutex> lock(channel.mutex);

    if(channel.completions.empty()) {
        return false;
    }

    completion = std::move(
        channel.completions.front()
    );

    channel.completions.pop_front();

    return true;
}


std::size_t WorkerPool::getWorkerCount() const {
    return workers.size();
}


void WorkerPool::stop() {
    {
        std::lock_guard<std::mutex> lock(jobs_mutex);

        if(stopping) {
            return;
        }

        stopping = true;
    }

    jobs_condition.notify_all();

    for(auto& worker : workers) {
        if(worker.joinable()) {
            worker.join();
        }
    }

    workers.clear();

    {
        std::lock_guard<std::mutex> lock(jobs_mutex);
        jobs.clear();
    }

    for(auto& channel : channels) {
        std::lock_guard<std::mutex> lock(channel->mutex);
        channel->completions.clear();
    }
}


void WorkerPool::workerLoop() {
    const char* host_environment =
        std::getenv("AORTA_REDIS_HOST");

    const char* port_environment =
        std::getenv("AORTA_REDIS_PORT");

    std::string redis_host =
        host_environment != nullptr
            ? host_environment
            : "redis";

    int redis_port = 6379;

    if(port_environment != nullptr) {
        try {
            redis_port = std::stoi(port_environment);
        } catch(...) {
            redis_port = 6379;
        }
    }

    if(redis_port <= 0 || redis_port > 65535) {
        redis_port = 6379;
    }

    RedisClient redis(
        redis_host,
        redis_port
    );

    while(true) {
        Job job;

        {
            std::unique_lock<std::mutex> lock(jobs_mutex);

            jobs_condition.wait(
                lock,
                [this]() {
                    return stopping || !jobs.empty();
                }
            );

            if(stopping && jobs.empty()) {
                return;
            }

            job = std::move(jobs.front());
            jobs.pop_front();
        }

        Completion completion;

        try {
            completion = job.work(redis);
        } catch(...) {
            completion = nullptr;
        }

        if(completion) {
            publishCompletion(
                job.reactor_id,
                std::move(completion)
            );
        }
    }
}


void WorkerPool::publishCompletion(
    std::size_t reactor_id,
    Completion completion
) {
    if(reactor_id >= channels.size() || !completion) {
        return;
    }

    auto& channel = *channels[reactor_id];

    {
        std::lock_guard<std::mutex> lock(channel.mutex);
        channel.completions.push_back(
            std::move(completion)
        );
    }

    const std::uint64_t value = 1;

    while(true) {
        const ssize_t result =
            write(
                channel.event_fd,
                &value,
                sizeof(value)
            );

        if(result == static_cast<ssize_t>(sizeof(value))) {
            return;
        }

        if(result == -1 && errno == EINTR) {
            continue;
        }

        return;
    }
}
