#include "TestRunner.hpp"

#include "server/Histogram.hpp"

#include <cstdint>

TEST(small_values_land_in_the_first_bucket) {
    CHECK_EQ(Histogram::bucketIndex(0), std::size_t(0));
    CHECK_EQ(Histogram::bucketIndex(63), std::size_t(0));
    CHECK_EQ(Histogram::bucketUpperBound(0), std::uint64_t(63));
}


TEST(bucket_boundaries_are_where_they_claim_to_be) {
    // First sub-bucket of 2^6 covers [64, 79].
    CHECK_EQ(Histogram::bucketIndex(64), std::size_t(1));
    CHECK_EQ(Histogram::bucketIndex(79), std::size_t(1));
    CHECK_EQ(Histogram::bucketUpperBound(1), std::uint64_t(79));

    // 80 opens the next sub-bucket.
    CHECK_EQ(Histogram::bucketIndex(80), std::size_t(2));

    // A new power of two opens a new group of four.
    CHECK_EQ(Histogram::bucketIndex(128), std::size_t(5));
}


TEST(bucket_index_never_decreases_as_the_value_grows) {
    std::size_t previous = 0;

    for(std::uint64_t value = 1; value < 5000000; value = value + 1 + value / 64) {

        const std::size_t index = Histogram::bucketIndex(value);

        CHECK(index >= previous);

        previous = index;
    }
}


TEST(every_value_falls_inside_its_bucket_bounds) {
    const std::uint64_t samples[] = {
        1, 63, 64, 100, 999, 1000, 4096, 50000, 1000000, 9999999
    };

    for(std::uint64_t value : samples) {

        const std::size_t index = Histogram::bucketIndex(value);

        CHECK(value <= Histogram::bucketUpperBound(index));
    }
}


TEST(huge_values_saturate_rather_than_wrap) {
    const std::size_t last = Histogram::BUCKET_COUNT - 1;

    CHECK_EQ(Histogram::bucketIndex(1ULL << 40), last);
    CHECK_EQ(Histogram::bucketIndex(~0ULL), last);
}


TEST(percentiles_of_a_uniform_spread) {
    Histogram histogram;

    // 1..1000 milliseconds expressed in microseconds.
    for(int i = 1; i <= 1000; ++i) {
        histogram.record(static_cast<std::uint64_t>(i) * 1000);
    }

    const Histogram::Counts counts = histogram.snapshot();

    CHECK_EQ(Histogram::total(counts), std::uint64_t(1000));

    const std::uint64_t p50 = Histogram::percentile(counts, 50.0);
    const std::uint64_t p99 = Histogram::percentile(counts, 99.0);

    /*
     * Buckets are approximate by design, so assert the band the true value
     * must fall in rather than an exact figure. The guarantee is that a
     * reported percentile never understates the real one.
     */
    CHECK(p50 >= 500000);
    CHECK(p50 <= 500000 + 500000 / 8);

    CHECK(p99 >= 990000);
    CHECK(p99 <= 990000 + 990000 / 8);

    CHECK(p99 > p50);
}


TEST(percentile_of_an_empty_histogram_is_zero) {
    Histogram histogram;

    CHECK_EQ(
        Histogram::percentile(histogram.snapshot(), 99.0),
        std::uint64_t(0)
    );
}


TEST(percentile_of_a_single_sample) {
    Histogram histogram;

    histogram.record(1000);

    const Histogram::Counts counts = histogram.snapshot();

    CHECK(Histogram::percentile(counts, 50.0) >= 1000);
    CHECK(Histogram::percentile(counts, 99.0) >= 1000);
}


TEST(merging_sums_bucket_counts) {
    Histogram first;
    Histogram second;

    for(int i = 0; i < 10; ++i) {
        first.record(1000);
    }

    for(int i = 0; i < 5; ++i) {
        second.record(1000);
    }

    Histogram::Counts merged = first.snapshot();

    Histogram::merge(merged, second.snapshot());

    CHECK_EQ(Histogram::total(merged), std::uint64_t(15));
}


TEST_MAIN("Histogram")
