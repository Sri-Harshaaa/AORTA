#include "server/Metrics.hpp"

#include <algorithm>
#include <sstream>

constexpr std::uint64_t Metrics::LATENCY_BUCKETS_US[
    Metrics::LATENCY_BUCKET_COUNT
];

void Metrics::incrementRequests() {
    requests_total.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::incrementResponses() {
    responses_total.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::incrementActiveConnections() {
    active_connections.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::decrementActiveConnections() {
    std::uint64_t current = active_connections.load(std::memory_order_relaxed);

    while(
        current > 0 &&
        !active_connections.compare_exchange_weak(
            current,
            current - 1,
            std::memory_order_relaxed,
            std::memory_order_relaxed
        )
    ) {
    }
}

void Metrics::incrementErrors() {
    errors_total.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::addBytesReceived(std::size_t bytes) {
    bytes_received.fetch_add(
        static_cast<std::uint64_t>(bytes),
        std::memory_order_relaxed
    );
}

void Metrics::addBytesSent(std::size_t bytes) {
    bytes_sent.fetch_add(
        static_cast<std::uint64_t>(bytes),
        std::memory_order_relaxed
    );
}

void Metrics::recordLatency(std::uint64_t microseconds) {
    latency_count.fetch_add(1, std::memory_order_relaxed);
    latency_total_us.fetch_add(microseconds, std::memory_order_relaxed);

    std::uint64_t current_max = latency_max_us.load(std::memory_order_relaxed);

    while(
        current_max < microseconds &&
        !latency_max_us.compare_exchange_weak(
            current_max,
            microseconds,
            std::memory_order_relaxed,
            std::memory_order_relaxed
        )
    ) {
    }

    for(std::size_t i = 0; i < LATENCY_BUCKET_COUNT; ++i) {
        if(microseconds <= LATENCY_BUCKETS_US[i]) {
            latency_buckets[i].fetch_add(
                1,
                std::memory_order_relaxed
            );
            return;
        }
    }

    latency_buckets[LATENCY_BUCKET_COUNT - 1].fetch_add(
        1,
        std::memory_order_relaxed
    );
}

std::size_t Metrics::getRequests() const {
    return static_cast<std::size_t>(
        requests_total.load(std::memory_order_relaxed)
    );
}

std::size_t Metrics::getResponses() const {
    return static_cast<std::size_t>(
        responses_total.load(std::memory_order_relaxed)
    );
}

std::size_t Metrics::getActiveConnections() const {
    return static_cast<std::size_t>(
        active_connections.load(std::memory_order_relaxed)
    );
}

std::size_t Metrics::getErrors() const {
    return static_cast<std::size_t>(
        errors_total.load(std::memory_order_relaxed)
    );
}

std::size_t Metrics::getBytesReceived() const {
    return static_cast<std::size_t>(
        bytes_received.load(std::memory_order_relaxed)
    );
}

std::size_t Metrics::getBytesSent() const {
    return static_cast<std::size_t>(
        bytes_sent.load(std::memory_order_relaxed)
    );
}

std::uint64_t Metrics::getLatencyCount() const {
    return latency_count.load(std::memory_order_relaxed);
}

double Metrics::getAverageLatencyMs() const {
    const std::uint64_t count =
        latency_count.load(std::memory_order_relaxed);

    if(count == 0) {
        return 0.0;
    }

    const std::uint64_t total_us =
        latency_total_us.load(std::memory_order_relaxed);

    return static_cast<double>(total_us) /
           static_cast<double>(count) /
           1000.0;
}

std::uint64_t Metrics::getP50LatencyUs() const {
    const std::uint64_t count =
        latency_count.load(std::memory_order_relaxed);

    if(count == 0) {
        return 0;
    }

    const std::uint64_t target =
        (count + 1) / 2;

    std::uint64_t cumulative = 0;

    for(std::size_t i = 0; i < LATENCY_BUCKET_COUNT; ++i) {
        cumulative += latency_buckets[i].load(
            std::memory_order_relaxed
        );

        if(cumulative >= target) {
            return LATENCY_BUCKETS_US[i];
        }
    }

    return LATENCY_BUCKETS_US[LATENCY_BUCKET_COUNT - 1];
}

std::uint64_t Metrics::getP95LatencyUs() const {
    const std::uint64_t count =
        latency_count.load(std::memory_order_relaxed);

    if(count == 0) {
        return 0;
    }

    const std::uint64_t target =
        (count * 95 + 99) / 100;

    std::uint64_t cumulative = 0;

    for(std::size_t i = 0; i < LATENCY_BUCKET_COUNT; ++i) {
        cumulative += latency_buckets[i].load(
            std::memory_order_relaxed
        );

        if(cumulative >= target) {
            return LATENCY_BUCKETS_US[i];
        }
    }

    return LATENCY_BUCKETS_US[LATENCY_BUCKET_COUNT - 1];
}

std::uint64_t Metrics::getP99LatencyUs() const {
    const std::uint64_t count =
        latency_count.load(std::memory_order_relaxed);

    if(count == 0) {
        return 0;
    }

    const std::uint64_t target =
        (count * 99 + 99) / 100;

    std::uint64_t cumulative = 0;

    for(std::size_t i = 0; i < LATENCY_BUCKET_COUNT; ++i) {
        cumulative += latency_buckets[i].load(
            std::memory_order_relaxed
        );

        if(cumulative >= target) {
            return LATENCY_BUCKETS_US[i];
        }
    }

    return LATENCY_BUCKETS_US[LATENCY_BUCKET_COUNT - 1];
}

std::uint64_t Metrics::getMaxLatencyUs() const {
    return latency_max_us.load(std::memory_order_relaxed);
}

std::string Metrics::serialize() const {
    std::ostringstream output;

    const std::uint64_t request_count =
        requests_total.load(std::memory_order_relaxed);

    const std::uint64_t response_count =
        responses_total.load(std::memory_order_relaxed);

    const std::uint64_t active_count =
        active_connections.load(std::memory_order_relaxed);

    const std::uint64_t error_count =
        errors_total.load(std::memory_order_relaxed);

    const std::uint64_t received_bytes =
        bytes_received.load(std::memory_order_relaxed);

    const std::uint64_t sent_bytes =
        bytes_sent.load(std::memory_order_relaxed);

    const std::uint64_t latency_samples =
        latency_count.load(std::memory_order_relaxed);

    const std::uint64_t total_latency_us =
        latency_total_us.load(std::memory_order_relaxed);

    output
        << "aorta_requests_total " << request_count << "\n"
        << "aorta_responses_total " << response_count << "\n"
        << "aorta_active_connections " << active_count << "\n"
        << "aorta_errors_total " << error_count << "\n"
        << "aorta_bytes_received_total " << received_bytes << "\n"
        << "aorta_bytes_sent_total " << sent_bytes << "\n"
        << "aorta_latency_samples_total " << latency_samples << "\n"
        << "aorta_latency_sum_seconds "
        << static_cast<double>(total_latency_us) / 1000000.0
        << "\n"
        << "aorta_latency_avg_ms "
        << getAverageLatencyMs()
        << "\n"
        << "aorta_latency_p50_ms "
        << static_cast<double>(getP50LatencyUs()) / 1000.0
        << "\n"
        << "aorta_latency_p95_ms "
        << static_cast<double>(getP95LatencyUs()) / 1000.0
        << "\n"
        << "aorta_latency_p99_ms "
        << static_cast<double>(getP99LatencyUs()) / 1000.0
        << "\n"
        << "aorta_latency_max_ms "
        << static_cast<double>(getMaxLatencyUs()) / 1000.0
        << "\n";

    std::uint64_t cumulative = 0;

    for(std::size_t i = 0; i < LATENCY_BUCKET_COUNT; ++i) {
        cumulative += latency_buckets[i].load(
            std::memory_order_relaxed
        );

        output
            << "aorta_latency_bucket{le=\""
            << LATENCY_BUCKETS_US[i]
            << "us\"} "
            << cumulative
            << "\n";
    }

    return output.str();
}
