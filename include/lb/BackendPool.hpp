#pragma once

#include "lb/Backend.hpp"

#include <cstddef>
#include <vector>

class BackendPool {

private:
    std::vector<Backend> backends;

public:
    void addBackend(const Backend& backend);

    std::size_t size() const;

    const Backend& getBackend(
        std::size_t index
    ) const;

    Backend& getBackend(
        std::size_t index
    );

    bool hasHealthyBackend() const;

    std::size_t healthyCount() const;

    void reset();

    const std::vector<Backend>& getBackends() const;
};