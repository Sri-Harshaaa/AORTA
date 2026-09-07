#include "lb/LbMetrics.hpp"

#include <sstream>

namespace {

void help(
    std::ostringstream& out,
    const char* name,
    const char* description,
    const char* type
) {
    out << "# HELP " << name << " " << description << "\n";
    out << "# TYPE " << name << " " << type << "\n";
}

}

std::string LbMetrics::render(
    const BackendPool& pool,
    std::size_t active_connections,
    const std::string& strategy,
    std::size_t buffered_bytes,
    std::size_t buffer_budget
) {
    std::ostringstream out;

    help(out, "aorta_lb_info",
         "Balancing strategy this process was started with.", "gauge");
    out << "aorta_lb_info{strategy=\"" << strategy << "\"} 1\n";

    help(out, "aorta_lb_active_connections",
         "Client connections currently being relayed.", "gauge");
    out << "aorta_lb_active_connections " << active_connections << "\n";

    help(out, "aorta_lb_backends_healthy",
         "Backends currently passing health checks.", "gauge");
    out << "aorta_lb_backends_healthy " << pool.healthyCount() << "\n";

    help(out, "aorta_lb_backends_total",
         "Backends configured.", "gauge");
    out << "aorta_lb_backends_total " << pool.size() << "\n";

    help(out, "aorta_lb_buffered_bytes",
         "Bytes held in relay buffers across all connections.", "gauge");
    out << "aorta_lb_buffered_bytes " << buffered_bytes << "\n";

    help(out, "aorta_lb_buffer_budget_bytes",
         "Relay memory budget; reads pause once buffered bytes reach it.", "gauge");
    out << "aorta_lb_buffer_budget_bytes " << buffer_budget << "\n";

    help(out, "aorta_lb_backend_healthy",
         "Whether one backend is currently routable.", "gauge");

    for(std::size_t i = 0; i < pool.size(); ++i) {

        const Backend& backend = pool.getBackend(i);

        out << "aorta_lb_backend_healthy{backend=\""
            << backend.host << ":" << backend.port << "\"} "
            << (backend.healthy ? 1 : 0) << "\n";
    }

    help(out, "aorta_lb_backend_active_connections",
         "Connections one backend is currently serving.", "gauge");

    for(std::size_t i = 0; i < pool.size(); ++i) {

        const Backend& backend = pool.getBackend(i);

        out << "aorta_lb_backend_active_connections{backend=\""
            << backend.host << ":" << backend.port << "\"} "
            << backend.connection_count << "\n";
    }

    help(out, "aorta_lb_backend_connections_total",
         "Connections ever routed to one backend.", "counter");

    for(std::size_t i = 0; i < pool.size(); ++i) {

        const Backend& backend = pool.getBackend(i);

        out << "aorta_lb_backend_connections_total{backend=\""
            << backend.host << ":" << backend.port << "\"} "
            << backend.total_connections << "\n";
    }

    help(out, "aorta_lb_backend_failed_connections_total",
         "Failed connect attempts to one backend.", "counter");

    for(std::size_t i = 0; i < pool.size(); ++i) {

        const Backend& backend = pool.getBackend(i);

        out << "aorta_lb_backend_failed_connections_total{backend=\""
            << backend.host << ":" << backend.port << "\"} "
            << backend.failed_connections << "\n";
    }

    help(out, "aorta_lb_backend_failovers_total",
         "Times a connection was moved off one backend.", "counter");

    for(std::size_t i = 0; i < pool.size(); ++i) {

        const Backend& backend = pool.getBackend(i);

        out << "aorta_lb_backend_failovers_total{backend=\""
            << backend.host << ":" << backend.port << "\"} "
            << backend.failovers << "\n";
    }

    return out.str();
}
