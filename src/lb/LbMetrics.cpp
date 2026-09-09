#include "lb/LbMetrics.hpp"

#include <sstream>

std::string LbMetrics::render(
    const BackendPool& pool,
    std::size_t active_connections
) {
    std::ostringstream output;

    output
        << "# HELP aorta_lb_active_connections Number of active client TCP connections at the load balancer.\n"
        << "# TYPE aorta_lb_active_connections gauge\n"
        << "aorta_lb_active_connections "
        << active_connections
        << "\n\n";

    output
        << "# HELP aorta_lb_healthy_backends Number of healthy backend servers.\n"
        << "# TYPE aorta_lb_healthy_backends gauge\n"
        << "aorta_lb_healthy_backends "
        << pool.healthyCount()
        << "\n\n";

    output
        << "# HELP aorta_lb_total_backends Total number of configured backend servers.\n"
        << "# TYPE aorta_lb_total_backends gauge\n"
        << "aorta_lb_total_backends "
        << pool.size()
        << "\n\n";

    output
        << "# HELP aorta_lb_backend_healthy Whether a backend is healthy (1) or unhealthy (0).\n"
        << "# TYPE aorta_lb_backend_healthy gauge\n";

    for (std::size_t i = 0; i < pool.size(); ++i) {
        const Backend& backend = pool.getBackend(i);

        output
            << "aorta_lb_backend_healthy{backend=\""
            << backend.host
            << "\",port=\""
            << backend.port
            << "\"} "
            << (backend.healthy ? 1 : 0)
            << "\n";
    }

    output << "\n";

    output
        << "# HELP aorta_lb_backend_connections Current active connections to a backend.\n"
        << "# TYPE aorta_lb_backend_connections gauge\n";

    for (std::size_t i = 0; i < pool.size(); ++i) {
        const Backend& backend = pool.getBackend(i);

        output
            << "aorta_lb_backend_connections{backend=\""
            << backend.host
            << "\",port=\""
            << backend.port
            << "\"} "
            << backend.connection_count
            << "\n";
    }

    output << "\n";

    output
        << "# HELP aorta_lb_backend_total_connections Total connections handled by a backend.\n"
        << "# TYPE aorta_lb_backend_total_connections counter\n";

    for (std::size_t i = 0; i < pool.size(); ++i) {
        const Backend& backend = pool.getBackend(i);

        output
            << "aorta_lb_backend_total_connections{backend=\""
            << backend.host
            << "\",port=\""
            << backend.port
            << "\"} "
            << backend.total_connections
            << "\n";
    }

    output << "\n";

    output
        << "# HELP aorta_lb_backend_failed_connections Total failed backend connections.\n"
        << "# TYPE aorta_lb_backend_failed_connections counter\n";

    for (std::size_t i = 0; i < pool.size(); ++i) {
        const Backend& backend = pool.getBackend(i);

        output
            << "aorta_lb_backend_failed_connections{backend=\""
            << backend.host
            << "\",port=\""
            << backend.port
            << "\"} "
            << backend.failed_connections
            << "\n";
    }

    output << "\n";

    output
        << "# HELP aorta_lb_backend_failovers Total backend failovers.\n"
        << "# TYPE aorta_lb_backend_failovers counter\n";

    for (std::size_t i = 0; i < pool.size(); ++i) {
        const Backend& backend = pool.getBackend(i);

        output
            << "aorta_lb_backend_failovers{backend=\""
            << backend.host
            << "\",port=\""
            << backend.port
            << "\"} "
            << backend.failovers
            << "\n";
    }

    return output.str();
}