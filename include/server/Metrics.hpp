#pragma once

#include "server/Histogram.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

/*
 * Per-reactor counters.
 *
 * Every reactor owns one of these and writes to it from its own thread. The
 * counters are atomic because /metrics is answered by whichever reactor the
 * kernel handed the scrape connection to, which then reads every other
 * reactor's instance through MetricsRegistry.
 *
 * Relaxed ordering throughout: these are monitoring counters, and no program
 * decision depends on observing them in any particular order.
 */
class Metrics {

public:
    struct Snapshot {
        std::uint64_t requests{0};
        std::uint64_t responses{0};
        std::uint64_t errors{0};
        std::uint64_t bytes_received{0};
        std::uint64_t bytes_sent{0};
        std::uint64_t accepted{0};
        std::uint64_t rejected{0};
        std::int64_t active_connections{0};
        std::uint64_t latency_sum_micros{0};
        double requests_per_second{0.0};
        Histogram::Counts latency{};
    };

    Metrics() = default;

    Metrics(const Metrics&) = delete;
    Metrics& operator=(const Metrics&) = delete;

    void incrementRequests();
    void incrementResponses();
    void incrementErrors();
    void incrementAccepted();
    void incrementRejected();

    void incrementActiveConnections();
    void decrementActiveConnections();

    void addBytesReceived(std::size_t bytes);
    void addBytesSent(std::size_t bytes);

    /*
     * Service latency: the interval between the first byte of a request
     * arriving and its response being fully serialized and queued for the
     * socket. It covers parsing, routing and any backend work such as a Redis
     * round trip. It deliberately excludes the time spent draining the
     * response to a slow client, which the server does not control.
     */
    void recordLatency(std::uint64_t microseconds);

    /*
     * Refreshes the requests-per-second gauge. Called from the owning
     * reactor's one second timer tick, and only from that thread.
     */
    void tick();

    Snapshot snapshot() const;

    static void merge(
        Snapshot& into,
        const Snapshot& from
    );

private:
    std::atomic<std::uint64_t> requests_total{0};
    std::atomic<std::uint64_t> responses_total{0};
    std::atomic<std::uint64_t> errors_total{0};
    std::atomic<std::uint64_t> accepted_total{0};
    std::atomic<std::uint64_t> rejected_total{0};
    std::atomic<std::uint64_t> bytes_received_total{0};
    std::atomic<std::uint64_t> bytes_sent_total{0};
    std::atomic<std::int64_t> active_connections{0};
    std::atomic<std::uint64_t> latency_sum_micros{0};

    /*
     * Stored in thousandths so the gauge stays an integer atomic, which is
     * lock-free on every platform this targets.
     */
    std::atomic<std::uint64_t> requests_per_second_milli{0};

    Histogram latency;

    // Touched only by the owning reactor thread, inside tick().
    std::uint64_t last_tick_requests{0};
    std::chrono::steady_clock::time_point last_tick_time{};
    bool has_last_tick{false};
};
