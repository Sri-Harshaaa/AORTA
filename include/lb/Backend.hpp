#pragma once

#include <cstddef>
#include <string>

struct Backend {

    std::string host;

    int port{0};

    bool healthy{true};

    int consecutive_failures{0};

    std::size_t connection_count{0};

    std::size_t total_connections{0};

    std::size_t failed_connections{0};

    std::size_t failovers{0};
};