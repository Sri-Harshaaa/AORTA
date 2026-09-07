#include "server/Histogram.hpp"

#include <bit>

std::size_t Histogram::bucketIndex(
    std::uint64_t microseconds
) {
    if(microseconds < (1ULL << MIN_EXPONENT)) {
        return 0;
    }

    const int exponent =
        63 - std::countl_zero(microseconds);

    if(exponent > MAX_EXPONENT) {
        return BUCKET_COUNT - 1;
    }

    /*
     * Values in [2^e, 2^(e+1)) are split into four sub-buckets of width
     * 2^(e-2). The two bits directly below the leading bit select one.
     */
    const std::uint64_t sub =
        (microseconds >> (exponent - 2)) & (SUB_BUCKETS - 1);

    return 1
        + static_cast<std::size_t>(exponent - MIN_EXPONENT) * SUB_BUCKETS
        + static_cast<std::size_t>(sub);
}


std::uint64_t Histogram::bucketUpperBound(
    std::size_t index
) {
    if(index == 0) {
        return (1ULL << MIN_EXPONENT) - 1;
    }

    if(index >= BUCKET_COUNT - 1) {
        return (1ULL << (MAX_EXPONENT + 1)) - 1;
    }

    const std::size_t offset = index - 1;

    const int exponent =
        MIN_EXPONENT
        + static_cast<int>(offset / SUB_BUCKETS);

    const std::uint64_t sub =
        static_cast<std::uint64_t>(offset % SUB_BUCKETS);

    return ((4ULL + sub + 1ULL) << (exponent - 2)) - 1ULL;
}


void Histogram::record(
    std::uint64_t microseconds
) {
    buckets[bucketIndex(microseconds)]
        .fetch_add(1, std::memory_order_relaxed);
}


Histogram::Counts Histogram::snapshot() const {
    Counts counts{};

    for(std::size_t i = 0; i < BUCKET_COUNT; ++i) {
        counts[i] =
            buckets[i].load(std::memory_order_relaxed);
    }

    return counts;
}


std::uint64_t Histogram::total(
    const Counts& counts
) {
    std::uint64_t sum = 0;

    for(std::uint64_t count : counts) {
        sum += count;
    }

    return sum;
}


std::uint64_t Histogram::percentile(
    const Counts& counts,
    double wanted
) {
    const std::uint64_t count = total(counts);

    if(count == 0) {
        return 0;
    }

    if(wanted < 0.0) {
        wanted = 0.0;
    }

    if(wanted > 100.0) {
        wanted = 100.0;
    }

    std::uint64_t rank =
        static_cast<std::uint64_t>(
            (wanted / 100.0) * static_cast<double>(count) + 0.5
        );

    if(rank == 0) {
        rank = 1;
    }

    if(rank > count) {
        rank = count;
    }

    std::uint64_t cumulative = 0;

    for(std::size_t i = 0; i < BUCKET_COUNT; ++i) {

        cumulative += counts[i];

        if(cumulative >= rank) {
            return bucketUpperBound(i);
        }
    }

    return bucketUpperBound(BUCKET_COUNT - 1);
}


void Histogram::merge(
    Counts& into,
    const Counts& from
) {
    for(std::size_t i = 0; i < BUCKET_COUNT; ++i) {
        into[i] += from[i];
    }
}
