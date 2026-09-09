#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <atomic>

class Metrics {

private:
    static constexpr std::size_t LATENCY_BUCKET_COUNT = 14;

    static constexpr std::uint64_t LATENCY_BUCKETS_US[LATENCY_BUCKET_COUNT] = {
        100,
        250,
        500,
        1000,
        2000,
        5000,
        10000,
        25000,
        50000,
        100000,
        250000,
        500000,
        1000000,
        5000000
    };

    std::atomic<std::uint64_t> requests_total{0};
    std::atomic<std::uint64_t> responses_total{0};
    std::atomic<std::uint64_t> active_connections{0};
    std::atomic<std::uint64_t> errors_total{0};
    std::atomic<std::uint64_t> bytes_received{0};
    std::atomic<std::uint64_t> bytes_sent{0};

    std::atomic<std::uint64_t> latency_count{0};
    std::atomic<std::uint64_t> latency_total_us{0};
    std::atomic<std::uint64_t> latency_max_us{0};
    std::array<std::atomic<std::uint64_t>, LATENCY_BUCKET_COUNT> latency_buckets{};

public:
    void incrementRequests();
    void incrementResponses();
    void incrementActiveConnections();
    void decrementActiveConnections();
    void incrementErrors();

    void addBytesReceived(std::size_t bytes);
    void addBytesSent(std::size_t bytes);

    void recordLatency(std::uint64_t microseconds);

    std::size_t getRequests() const;
    std::size_t getResponses() const;
    std::size_t getActiveConnections() const;
    std::size_t getErrors() const;
    std::size_t getBytesReceived() const;
    std::size_t getBytesSent() const;

    std::uint64_t getLatencyCount() const;
    double getAverageLatencyMs() const;
    std::uint64_t getP50LatencyUs() const;
    std::uint64_t getP95LatencyUs() const;
    std::uint64_t getP99LatencyUs() const;
    std::uint64_t getMaxLatencyUs() const;

    std::string serialize() const;
};
