#include "server/Metrics.hpp"

namespace {

constexpr std::memory_order RELAXED = std::memory_order_relaxed;

}

void Metrics::incrementRequests() {
    requests_total.fetch_add(1, RELAXED);
}


void Metrics::incrementResponses() {
    responses_total.fetch_add(1, RELAXED);
}


void Metrics::incrementErrors() {
    errors_total.fetch_add(1, RELAXED);
}


void Metrics::incrementAccepted() {
    accepted_total.fetch_add(1, RELAXED);
}


void Metrics::incrementRejected() {
    rejected_total.fetch_add(1, RELAXED);
}


void Metrics::incrementActiveConnections() {
    active_connections.fetch_add(1, RELAXED);
}


void Metrics::decrementActiveConnections() {
    active_connections.fetch_sub(1, RELAXED);
}


void Metrics::addBytesReceived(std::size_t bytes) {
    bytes_received_total.fetch_add(
        static_cast<std::uint64_t>(bytes),
        RELAXED
    );
}


void Metrics::addBytesSent(std::size_t bytes) {
    bytes_sent_total.fetch_add(
        static_cast<std::uint64_t>(bytes),
        RELAXED
    );
}


void Metrics::recordLatency(std::uint64_t microseconds) {
    latency.record(microseconds);

    latency_sum_micros.fetch_add(microseconds, RELAXED);
}


void Metrics::tick() {
    const auto now = std::chrono::steady_clock::now();

    const std::uint64_t requests =
        requests_total.load(RELAXED);

    if(has_last_tick) {

        const double elapsed_seconds =
            std::chrono::duration<double>(
                now - last_tick_time
            ).count();

        if(elapsed_seconds > 0.0) {

            const double rate =
                static_cast<double>(requests - last_tick_requests)
                / elapsed_seconds;

            requests_per_second_milli.store(
                static_cast<std::uint64_t>(rate * 1000.0 + 0.5),
                RELAXED
            );
        }
    }

    last_tick_requests = requests;
    last_tick_time = now;
    has_last_tick = true;
}


Metrics::Snapshot Metrics::snapshot() const {
    Snapshot result;

    result.requests = requests_total.load(RELAXED);
    result.responses = responses_total.load(RELAXED);
    result.errors = errors_total.load(RELAXED);
    result.accepted = accepted_total.load(RELAXED);
    result.rejected = rejected_total.load(RELAXED);
    result.bytes_received = bytes_received_total.load(RELAXED);
    result.bytes_sent = bytes_sent_total.load(RELAXED);
    result.active_connections = active_connections.load(RELAXED);
    result.latency_sum_micros = latency_sum_micros.load(RELAXED);

    result.requests_per_second =
        static_cast<double>(
            requests_per_second_milli.load(RELAXED)
        ) / 1000.0;

    result.latency = latency.snapshot();

    return result;
}


void Metrics::merge(
    Snapshot& into,
    const Snapshot& from
) {
    into.requests += from.requests;
    into.responses += from.responses;
    into.errors += from.errors;
    into.accepted += from.accepted;
    into.rejected += from.rejected;
    into.bytes_received += from.bytes_received;
    into.bytes_sent += from.bytes_sent;
    into.active_connections += from.active_connections;
    into.latency_sum_micros += from.latency_sum_micros;
    into.requests_per_second += from.requests_per_second;

    Histogram::merge(into.latency, from.latency);
}
