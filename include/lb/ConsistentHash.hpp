#pragma once

#include "lb/BackendPool.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

class ConsistentHash {

private:
    static constexpr std::size_t VIRTUAL_NODES_PER_BACKEND = 128;

    std::map<std::uint64_t, std::size_t> ring;

    static std::uint64_t hash(
        const std::string& key
    );

    static std::string backendKey(
        const Backend& backend
    );

public:
    ConsistentHash() = default;

    void build(
        const BackendPool& pool
    );

    std::optional<std::size_t> getBackend(
        const std::string& client_key,
        const BackendPool& pool
    ) const;

    void clear();

    std::size_t size() const;
};