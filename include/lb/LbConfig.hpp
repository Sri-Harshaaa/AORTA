#pragma once

#include <cstddef>
#include <string>

/*
 * Runtime configuration for the load balancer. Everything here used to be a
 * compile-time constant, which made it impossible to run two balancers with
 * different strategies side by side - the whole point of the benchmark.
 */
struct LbConfig {

    int listen_port{9000};

    std::string backends_path{"config/backends.conf"};

    /*
     * Round robin by default. Consistent hashing routes on client IP, so with
     * a single load generator it sends every connection to one backend, which
     * makes it the wrong default for a tool whose job is to spread load.
     */
    std::string strategy{"round_robin"};

    int health_interval{5};

    std::size_t max_connections{100000};

    // Per-direction cap before a connection is dropped.
    std::size_t max_connection_buffer{8 * 1024 * 1024};

    /*
     * Process-wide relay memory budget. Without this, a few thousand congested
     * connections can exhaust RAM long before the descriptor limit is reached.
     * Reads are paused once this is crossed and resume as buffers drain.
     */
    std::size_t max_total_buffer{512ULL * 1024 * 1024};

    static bool parse(
        int argc,
        char* argv[],
        LbConfig& config,
        std::string& error
    );

    static void printUsage(const char* program);

    void print() const;
};
