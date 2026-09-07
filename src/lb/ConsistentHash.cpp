#include "lb/ConsistentHash.hpp"

#include <sstream>

std::uint64_t ConsistentHash::hash(
    const std::string& key
) {
    /*
     * 64-bit FNV-1a.
     *
     * Unlike std::hash, this gives us a deterministic hash function
     * whose behavior does not depend on the standard library
     * implementation.
     */
    constexpr std::uint64_t FNV_OFFSET =
        14695981039346656037ULL;

    constexpr std::uint64_t FNV_PRIME =
        1099511628211ULL;

    std::uint64_t value = FNV_OFFSET;

    for(unsigned char byte : key) {

        value ^= byte;
        value *= FNV_PRIME;
    }

    return value;
}

std::string ConsistentHash::backendKey(
    const Backend& backend
) {
    return backend.host +
        ":" +
        std::to_string(backend.port);
}

void ConsistentHash::build(
    const BackendPool& pool
) {
    ring.clear();

    for(std::size_t backend_index = 0;
        backend_index < pool.size();
        ++backend_index) {

        const Backend& backend =
            pool.getBackend(backend_index);

        const std::string key =
            backendKey(backend);

        for(
            std::size_t virtual_node = 0;
            virtual_node < VIRTUAL_NODES_PER_BACKEND;
            ++virtual_node
        ) {

            const std::string virtual_key =
                key +
                "#" +
                std::to_string(virtual_node);

            ring.emplace(
                hash(virtual_key),
                backend_index
            );
        }
    }
}

std::optional<std::size_t> ConsistentHash::getBackend(
    const std::string& client_key,
    const BackendPool& pool
) const {
    if(
        ring.empty() ||
        pool.size() == 0
    ) {
        return std::nullopt;
    }

    const std::uint64_t client_hash =
        hash(client_key);

    auto iterator =
        ring.lower_bound(client_hash);

    if(iterator == ring.end()) {
        iterator = ring.begin();
    }

    const auto start =
        iterator;

    do {

        const std::size_t backend_index =
            iterator->second;

        if(
            backend_index < pool.size() &&
            pool.getBackend(backend_index).healthy
        ) {
            return backend_index;
        }

        ++iterator;

        if(iterator == ring.end()) {
            iterator = ring.begin();
        }

    } while(iterator != start);

    return std::nullopt;
}

void ConsistentHash::clear() {
    ring.clear();
}

std::size_t ConsistentHash::size() const {
    return ring.size();
}