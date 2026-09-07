#include "lb/BalancingStrategy.hpp"

void BalancingStrategy::rebuild(const BackendPool&) {
}


const char* BalancingStrategy::validNames() {
    return "round_robin, least_connections, consistent_hash";
}


bool BalancingStrategy::isValid(const std::string& name) {
    return name == "round_robin"
        || name == "least_connections"
        || name == "consistent_hash";
}


std::unique_ptr<BalancingStrategy> BalancingStrategy::create(
    const std::string& name
) {
    if(name == "least_connections") {
        return std::make_unique<LeastConnectionsStrategy>();
    }

    if(name == "consistent_hash") {
        return std::make_unique<ConsistentHashStrategy>();
    }

    if(name == "round_robin") {
        return std::make_unique<RoundRobinStrategy>();
    }

    return nullptr;
}


std::optional<std::size_t> RoundRobinStrategy::select(
    const BackendPool& pool,
    const std::string&
) {
    const std::size_t size = pool.size();

    if(size == 0) {
        return std::nullopt;
    }

    for(std::size_t attempt = 0; attempt < size; ++attempt) {

        const std::size_t index = next % size;

        ++next;

        if(pool.getBackend(index).healthy) {
            return index;
        }
    }

    return std::nullopt;
}


const char* RoundRobinStrategy::name() const {
    return "round_robin";
}


std::optional<std::size_t> LeastConnectionsStrategy::select(
    const BackendPool& pool,
    const std::string&
) {
    const std::size_t size = pool.size();

    if(size == 0) {
        return std::nullopt;
    }

    std::optional<std::size_t> best;
    std::size_t best_count = 0;

    for(std::size_t offset = 0; offset < size; ++offset) {

        const std::size_t index = (rotor + offset) % size;

        const Backend& backend = pool.getBackend(index);

        if(!backend.healthy) {
            continue;
        }

        if(
            !best.has_value() ||
            backend.connection_count < best_count
        ) {
            best = index;
            best_count = backend.connection_count;
        }
    }

    ++rotor;

    return best;
}


const char* LeastConnectionsStrategy::name() const {
    return "least_connections";
}


std::optional<std::size_t> ConsistentHashStrategy::select(
    const BackendPool& pool,
    const std::string& client_key
) {
    return ring.getBackend(client_key, pool);
}


void ConsistentHashStrategy::rebuild(const BackendPool& pool) {
    ring.build(pool);
}


const char* ConsistentHashStrategy::name() const {
    return "consistent_hash";
}


std::size_t ConsistentHashStrategy::ringSize() const {
    return ring.size();
}
