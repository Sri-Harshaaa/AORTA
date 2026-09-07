#pragma once

#include "lb/BackendPool.hpp"
#include "lb/ConsistentHash.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

/*
 * How the balancer picks a backend for a new connection.
 *
 * All three implementations skip unhealthy backends, so a strategy never has
 * to be told about health separately - the health checker mutates the pool and
 * selection sees it on the next call.
 *
 * No locking anywhere: the balancer is a single event loop thread, so the
 * round-robin counter and the least-connections rotor are plain integers.
 */
class BalancingStrategy {

public:
    virtual ~BalancingStrategy() = default;

    virtual std::optional<std::size_t> select(
        const BackendPool& pool,
        const std::string& client_key
    ) = 0;

    /*
     * Called once at startup and whenever pool membership changes. Only the
     * hash ring needs it.
     */
    virtual void rebuild(const BackendPool& pool);

    virtual const char* name() const = 0;

    static std::unique_ptr<BalancingStrategy> create(
        const std::string& name
    );

    static bool isValid(const std::string& name);

    static const char* validNames();
};


/*
 * Hands out backends in order. The default, because it is the only one of the
 * three whose distribution does not depend on where the clients come from.
 */
class RoundRobinStrategy : public BalancingStrategy {

public:
    std::optional<std::size_t> select(
        const BackendPool& pool,
        const std::string& client_key
    ) override;

    const char* name() const override;

private:
    std::size_t next{0};
};


/*
 * Sends each connection to the healthy backend holding the fewest. Ties are
 * broken by a rotating start offset, so an idle pool still spreads evenly
 * instead of piling onto backend 0.
 */
class LeastConnectionsStrategy : public BalancingStrategy {

public:
    std::optional<std::size_t> select(
        const BackendPool& pool,
        const std::string& client_key
    ) override;

    const char* name() const override;

private:
    std::size_t rotor{0};
};


/*
 * Stable routing by client key, so the same client keeps landing on the same
 * backend and removing one backend remaps only its share.
 *
 * Note for benchmarking: every connection from one load generator carries the
 * same source IP and therefore lands on one backend. That is correct affinity
 * behaviour and useless as a load test. Use it to demonstrate stable routing
 * across a backend removal, not to measure throughput.
 */
class ConsistentHashStrategy : public BalancingStrategy {

public:
    std::optional<std::size_t> select(
        const BackendPool& pool,
        const std::string& client_key
    ) override;

    void rebuild(const BackendPool& pool) override;

    const char* name() const override;

    std::size_t ringSize() const;

private:
    ConsistentHash ring;
};
