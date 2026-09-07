#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

/*
 * Fixed-bucket latency histogram.
 *
 * Recording a sample is one relaxed atomic increment with no allocation and no
 * locking, so it is safe to call from inside an event loop on the hot path.
 *
 * Layout: bucket 0 collects everything below 64us. Above that, each power of
 * two is split into four linear sub-buckets, which bounds the relative error
 * of any reported percentile at 1/8. That is enough resolution to report
 * P50/P95/P99 honestly without the memory or complexity of a full
 * HdrHistogram, and the whole structure is 656 bytes.
 */
class Histogram {

public:
    static constexpr int MIN_EXPONENT = 6;   // 64us
    static constexpr int MAX_EXPONENT = 25;  // ~33.5s

    static constexpr std::size_t SUB_BUCKETS = 4;

    static constexpr std::size_t BUCKET_COUNT =
        1
        + static_cast<std::size_t>(MAX_EXPONENT - MIN_EXPONENT + 1)
            * SUB_BUCKETS
        + 1;

    using Counts = std::array<std::uint64_t, BUCKET_COUNT>;

    Histogram() = default;

    Histogram(const Histogram&) = delete;
    Histogram& operator=(const Histogram&) = delete;

    void record(std::uint64_t microseconds);

    Counts snapshot() const;

    static std::size_t bucketIndex(std::uint64_t microseconds);

    /*
     * Inclusive upper bound of a bucket, in microseconds. The final bucket is
     * unbounded above; it reports the largest value that still lands in a
     * finite bucket.
     */
    static std::uint64_t bucketUpperBound(std::size_t index);

    static std::uint64_t total(const Counts& counts);

    /*
     * Percentile over merged counts, in microseconds. `wanted` is in [0, 100].
     * The value returned is the upper bound of the bucket the requested rank
     * falls into, so it never understates latency.
     */
    static std::uint64_t percentile(
        const Counts& counts,
        double wanted
    );

    static void merge(
        Counts& into,
        const Counts& from
    );

private:
    std::array<std::atomic<std::uint64_t>, BUCKET_COUNT> buckets{};
};
