#include "TestRunner.hpp"

#include "lb/BalancingStrategy.hpp"

#include <map>
#include <string>

namespace {

BackendPool makePool(int count) {
    BackendPool pool;

    for(int i = 0; i < count; ++i) {
        Backend backend;
        backend.host = "10.0.0." + std::to_string(i + 1);
        backend.port = 8080 + i;
        backend.healthy = true;

        pool.addBackend(backend);
    }

    return pool;
}

}


TEST(round_robin_visits_every_backend_in_order) {
    BackendPool pool = makePool(3);

    RoundRobinStrategy strategy;

    CHECK_EQ(strategy.select(pool, "a").value(), std::size_t(0));
    CHECK_EQ(strategy.select(pool, "b").value(), std::size_t(1));
    CHECK_EQ(strategy.select(pool, "c").value(), std::size_t(2));
    CHECK_EQ(strategy.select(pool, "d").value(), std::size_t(0));
}


TEST(round_robin_spreads_evenly_regardless_of_client_key) {
    BackendPool pool = makePool(3);

    RoundRobinStrategy strategy;

    std::map<std::size_t, int> hits;

    /*
     * Every request carries the same client key, which is exactly what a
     * single-host load generator looks like. Round robin must still spread.
     */
    for(int i = 0; i < 300; ++i) {
        hits[strategy.select(pool, "127.0.0.1").value()]++;
    }

    CHECK_EQ(hits.size(), std::size_t(3));
    CHECK_EQ(hits[0], 100);
    CHECK_EQ(hits[1], 100);
    CHECK_EQ(hits[2], 100);
}


TEST(round_robin_skips_unhealthy_backends) {
    BackendPool pool = makePool(3);

    pool.getBackend(1).healthy = false;

    RoundRobinStrategy strategy;

    for(int i = 0; i < 20; ++i) {
        CHECK(strategy.select(pool, "x").value() != std::size_t(1));
    }
}


TEST(round_robin_gives_up_when_nothing_is_healthy) {
    BackendPool pool = makePool(2);

    pool.getBackend(0).healthy = false;
    pool.getBackend(1).healthy = false;

    RoundRobinStrategy strategy;

    CHECK(!strategy.select(pool, "x").has_value());
}


TEST(least_connections_picks_the_quietest_backend) {
    BackendPool pool = makePool(3);

    pool.getBackend(0).connection_count = 10;
    pool.getBackend(1).connection_count = 2;
    pool.getBackend(2).connection_count = 7;

    LeastConnectionsStrategy strategy;

    CHECK_EQ(strategy.select(pool, "x").value(), std::size_t(1));
}


TEST(least_connections_ignores_a_quiet_but_unhealthy_backend) {
    BackendPool pool = makePool(3);

    pool.getBackend(0).connection_count = 10;
    pool.getBackend(1).connection_count = 0;
    pool.getBackend(1).healthy = false;
    pool.getBackend(2).connection_count = 7;

    LeastConnectionsStrategy strategy;

    CHECK_EQ(strategy.select(pool, "x").value(), std::size_t(2));
}


TEST(least_connections_breaks_ties_by_rotating) {
    BackendPool pool = makePool(3);

    LeastConnectionsStrategy strategy;

    std::map<std::size_t, int> hits;

    /*
     * With every backend idle, a naive implementation returns index 0 forever.
     * The rotor exists so an empty pool still fans out.
     */
    for(int i = 0; i < 30; ++i) {
        hits[strategy.select(pool, "x").value()]++;
    }

    CHECK_EQ(hits.size(), std::size_t(3));
}


TEST(consistent_hash_is_stable_for_the_same_key) {
    BackendPool pool = makePool(4);

    ConsistentHashStrategy strategy;
    strategy.rebuild(pool);

    const std::size_t first = strategy.select(pool, "client-a").value();

    for(int i = 0; i < 50; ++i) {
        CHECK_EQ(strategy.select(pool, "client-a").value(), first);
    }
}


TEST(consistent_hash_builds_a_ring_of_virtual_nodes) {
    BackendPool pool = makePool(4);

    ConsistentHashStrategy strategy;
    strategy.rebuild(pool);

    // 128 virtual nodes per backend.
    CHECK_EQ(strategy.ringSize(), std::size_t(4 * 128));
}


TEST(consistent_hash_remaps_only_a_share_when_a_backend_is_removed) {
    BackendPool full = makePool(4);

    ConsistentHashStrategy before;
    before.rebuild(full);

    std::map<std::string, std::size_t> original;

    for(int i = 0; i < 400; ++i) {
        const std::string key = "client-" + std::to_string(i);
        original[key] = before.select(full, key).value();
    }

    // Take the last backend out of rotation.
    full.getBackend(3).healthy = false;

    int moved = 0;

    for(const auto& entry : original) {

        const std::size_t now = before.select(full, entry.first).value();

        if(now != entry.second) {
            ++moved;
        }
    }

    /*
     * Only keys that were on the removed backend should move. With four
     * backends that is about a quarter; allow generous slack for hash
     * distribution while still failing a strategy that reshuffles everything.
     */
    CHECK(moved > 0);
    CHECK(moved < 200);
}


TEST(consistent_hash_sends_one_client_key_to_one_backend) {
    BackendPool pool = makePool(3);

    ConsistentHashStrategy strategy;
    strategy.rebuild(pool);

    std::map<std::size_t, int> hits;

    for(int i = 0; i < 100; ++i) {
        hits[strategy.select(pool, "127.0.0.1").value()]++;
    }

    /*
     * This is the documented benchmarking hazard, pinned as a test: one source
     * IP means one backend, so consistent hashing must not be the default.
     */
    CHECK_EQ(hits.size(), std::size_t(1));
}


TEST(factory_builds_each_named_strategy) {
    CHECK(BalancingStrategy::create("round_robin") != nullptr);
    CHECK(BalancingStrategy::create("least_connections") != nullptr);
    CHECK(BalancingStrategy::create("consistent_hash") != nullptr);
    CHECK(BalancingStrategy::create("nonsense") == nullptr);

    CHECK(BalancingStrategy::isValid("round_robin"));
    CHECK(!BalancingStrategy::isValid("nonsense"));
}


TEST(every_strategy_handles_an_empty_pool) {
    BackendPool empty;

    RoundRobinStrategy round_robin;
    LeastConnectionsStrategy least;
    ConsistentHashStrategy hash;

    hash.rebuild(empty);

    CHECK(!round_robin.select(empty, "x").has_value());
    CHECK(!least.select(empty, "x").has_value());
    CHECK(!hash.select(empty, "x").has_value());
}


TEST_MAIN("BalancingStrategy")
