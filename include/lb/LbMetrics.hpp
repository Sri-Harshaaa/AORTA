#pragma once

#include "lb/BackendPool.hpp"

#include <string>

class LbMetrics {

public:
    static std::string render(
        const BackendPool& pool,
        std::size_t active_connections
    );
};