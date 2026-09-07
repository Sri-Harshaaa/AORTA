#pragma once

#include "lb/BackendPool.hpp"

#include <cstddef>
#include <string>

class LbMetrics {

public:
    /*
     * Prometheus exposition, so the balancer and the backends can be scraped
     * by the same tooling.
     */
    static std::string render(
        const BackendPool& pool,
        std::size_t active_connections,
        const std::string& strategy,
        std::size_t buffered_bytes,
        std::size_t buffer_budget
    );
};
