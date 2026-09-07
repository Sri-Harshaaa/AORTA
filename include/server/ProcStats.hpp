#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>

/*
 * Process-level resource sampling from /proc/self.
 *
 * CPU is a delta between consecutive calls, so the first sample after
 * construction reports zero and every later one covers the interval since the
 * previous call. Percentages are of a single core, so a fully loaded eight
 * core machine reports about 800.
 */
class ProcStats {

public:
    struct Sample {
        double cpu_percent{0.0};
        std::size_t rss_bytes{0};
        std::size_t open_fds{0};
        std::size_t threads{0};
    };

    Sample sample();

private:
    std::mutex mutex;

    unsigned long long previous_cpu_ticks{0};
    std::chrono::steady_clock::time_point previous_time{};
    bool has_previous{false};

    static bool readCpuTicksAndThreads(
        unsigned long long& ticks,
        std::size_t& threads
    );

    static std::size_t readRssBytes();

    static std::size_t countOpenFds();
};
