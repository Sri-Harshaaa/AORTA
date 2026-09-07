#pragma once

#include "server/Metrics.hpp"
#include "server/ProcStats.hpp"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

/*
 * Collects every reactor's counters so a scrape reports the whole process.
 *
 * Without this, /metrics is answered by whichever reactor the kernel handed
 * the scrape connection to under SO_REUSEPORT, and reports only that one
 * reactor's slice - roughly 1/N of real traffic, jumping between scrapes.
 *
 * Registration happens on the main thread before any reactor thread starts.
 * The mutex guards the vector against a scrape racing a late registration.
 */
class MetricsRegistry {

public:
    void registerSource(
        int reactor_id,
        const Metrics* metrics
    );

    void setMode(const std::string& mode);

    Metrics::Snapshot aggregate() const;

    std::string renderPrometheus() const;

private:
    mutable std::mutex mutex;

    std::vector<std::pair<int, const Metrics*>> sources;

    std::string mode{"epoll"};

    mutable ProcStats proc_stats;
};
