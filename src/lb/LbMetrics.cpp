#include "lb/LbMetrics.hpp"

#include <sstream>

std::string LbMetrics::render(
    const BackendPool& pool,
    std::size_t active_connections
) {
    std::ostringstream output;

    output
        << "AORTA Load Balancer\n"
        << "active_connections "
        << active_connections
        << "\n"
        << "healthy_backends "
        << pool.healthyCount()
        << "\n"
        << "total_backends "
        << pool.size()
        << "\n\n";

    for(std::size_t i = 0; i < pool.size(); ++i) {

        const Backend& backend =
            pool.getBackend(i);

        output
            << "backend{"
            << "host=\"" << backend.host << "\","
            << "port=\"" << backend.port << "\""
            << "} "
            << "healthy=" << (backend.healthy ? 1 : 0)
            << " connections=" << backend.connection_count
            << " total_connections=" << backend.total_connections
            << " failed_connections=" << backend.failed_connections
            << " failovers=" << backend.failovers
            << "\n";
    }

    return output.str();
}