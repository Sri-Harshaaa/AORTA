#include "server/MetricsRegistry.hpp"

#include <cstdio>
#include <sstream>

namespace {

std::string seconds(std::uint64_t microseconds) {
    char buffer[32];

    std::snprintf(
        buffer,
        sizeof(buffer),
        "%.6f",
        static_cast<double>(microseconds) / 1000000.0
    );

    return std::string(buffer);
}

std::string decimal(double value, int precision) {
    char buffer[64];

    std::snprintf(
        buffer,
        sizeof(buffer),
        "%.*f",
        precision,
        value
    );

    return std::string(buffer);
}

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


void MetricsRegistry::registerSource(
    int reactor_id,
    const Metrics* metrics
) {
    if(metrics == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex);

    sources.emplace_back(reactor_id, metrics);
}


void MetricsRegistry::setMode(const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex);

    mode = value;
}


Metrics::Snapshot MetricsRegistry::aggregate() const {
    std::lock_guard<std::mutex> lock(mutex);

    Metrics::Snapshot total;

    for(const auto& source : sources) {
        Metrics::merge(
            total,
            source.second->snapshot()
        );
    }

    return total;
}


std::string MetricsRegistry::renderPrometheus() const {
    std::vector<std::pair<int, Metrics::Snapshot>> per_reactor;
    std::string current_mode;

    {
        std::lock_guard<std::mutex> lock(mutex);

        current_mode = mode;

        per_reactor.reserve(sources.size());

        for(const auto& source : sources) {
            per_reactor.emplace_back(
                source.first,
                source.second->snapshot()
            );
        }
    }

    Metrics::Snapshot total;

    for(const auto& entry : per_reactor) {
        Metrics::merge(total, entry.second);
    }

    const ProcStats::Sample process = proc_stats.sample();

    std::ostringstream out;

    help(out, "aorta_build_info",
         "Concurrency model this process was started with.", "gauge");
    out << "aorta_build_info{mode=\"" << current_mode << "\","
        << "workers=\"" << per_reactor.size() << "\"} 1\n";

    help(out, "aorta_requests_total",
         "Complete HTTP requests parsed.", "counter");
    out << "aorta_requests_total " << total.requests << "\n";

    help(out, "aorta_responses_total",
         "Responses fully written to a client socket.", "counter");
    out << "aorta_responses_total " << total.responses << "\n";

    help(out, "aorta_errors_total",
         "Requests dropped for a parse, protocol or socket error.", "counter");
    out << "aorta_errors_total " << total.errors << "\n";

    help(out, "aorta_connections_accepted_total",
         "Connections accepted since start.", "counter");
    out << "aorta_connections_accepted_total " << total.accepted << "\n";

    help(out, "aorta_connections_rejected_total",
         "Connections refused because a concurrency limit was reached.", "counter");
    out << "aorta_connections_rejected_total " << total.rejected << "\n";

    help(out, "aorta_active_connections",
         "Connections currently held open.", "gauge");
    out << "aorta_active_connections " << total.active_connections << "\n";

    help(out, "aorta_bytes_received_total",
         "Bytes read from client sockets.", "counter");
    out << "aorta_bytes_received_total " << total.bytes_received << "\n";

    help(out, "aorta_bytes_sent_total",
         "Bytes written to client sockets.", "counter");
    out << "aorta_bytes_sent_total " << total.bytes_sent << "\n";

    help(out, "aorta_requests_per_second",
         "Request rate over the last one second tick, summed across workers.", "gauge");
    out << "aorta_requests_per_second "
        << decimal(total.requests_per_second, 2) << "\n";

    /*
     * Percentile gauges are redundant with the histogram below, but they let
     * the bundled dashboard read P50/P95/P99 without doing bucket arithmetic
     * in the browser.
     */
    help(out, "aorta_request_latency_seconds",
         "Service latency percentiles over the process lifetime.", "gauge");

    out << "aorta_request_latency_seconds{quantile=\"0.5\"} "
        << seconds(Histogram::percentile(total.latency, 50.0)) << "\n";
    out << "aorta_request_latency_seconds{quantile=\"0.95\"} "
        << seconds(Histogram::percentile(total.latency, 95.0)) << "\n";
    out << "aorta_request_latency_seconds{quantile=\"0.99\"} "
        << seconds(Histogram::percentile(total.latency, 99.0)) << "\n";
    out << "aorta_request_latency_seconds{quantile=\"0.999\"} "
        << seconds(Histogram::percentile(total.latency, 99.9)) << "\n";

    const std::uint64_t latency_count =
        Histogram::total(total.latency);

    help(out, "aorta_request_duration_seconds",
         "Time from first request byte received to response queued.", "histogram");

    std::uint64_t cumulative = 0;

    for(std::size_t i = 0; i < Histogram::BUCKET_COUNT; ++i) {

        cumulative += total.latency[i];

        if(i == Histogram::BUCKET_COUNT - 1) {
            break;
        }

        out << "aorta_request_duration_seconds_bucket{le=\""
            << seconds(Histogram::bucketUpperBound(i))
            << "\"} "
            << cumulative
            << "\n";
    }

    out << "aorta_request_duration_seconds_bucket{le=\"+Inf\"} "
        << latency_count << "\n";

    out << "aorta_request_duration_seconds_sum "
        << seconds(total.latency_sum_micros) << "\n";

    out << "aorta_request_duration_seconds_count "
        << latency_count << "\n";

    help(out, "aorta_process_cpu_percent",
         "Process CPU use since the previous scrape, as a percentage of one core.", "gauge");
    out << "aorta_process_cpu_percent "
        << decimal(process.cpu_percent, 2) << "\n";

    help(out, "aorta_process_resident_memory_bytes",
         "Resident set size.", "gauge");
    out << "aorta_process_resident_memory_bytes "
        << process.rss_bytes << "\n";

    help(out, "aorta_process_open_fds",
         "Open file descriptors, the limit that usually caps connection count.", "gauge");
    out << "aorta_process_open_fds " << process.open_fds << "\n";

    help(out, "aorta_process_threads",
         "OS threads in this process.", "gauge");
    out << "aorta_process_threads " << process.threads << "\n";

    /*
     * Per-worker breakdown. Under SO_REUSEPORT the spread across reactors is
     * itself a result worth looking at, because the kernel does not divide
     * accepts evenly.
     */
    help(out, "aorta_worker_requests_total",
         "Complete requests handled by one worker.", "counter");

    for(const auto& entry : per_reactor) {
        out << "aorta_worker_requests_total{worker=\""
            << entry.first << "\"} "
            << entry.second.requests << "\n";
    }

    help(out, "aorta_worker_active_connections",
         "Connections currently held by one worker.", "gauge");

    for(const auto& entry : per_reactor) {
        out << "aorta_worker_active_connections{worker=\""
            << entry.first << "\"} "
            << entry.second.active_connections << "\n";
    }

    return out.str();
}
