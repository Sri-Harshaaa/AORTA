#include "lb/BackendPool.hpp"

void BackendPool::addBackend(
    const Backend& backend
) {
    backends.push_back(backend);
}

std::size_t BackendPool::size() const {
    return backends.size();
}

const Backend& BackendPool::getBackend(
    std::size_t index
) const {
    return backends.at(index);
}

Backend& BackendPool::getBackend(
    std::size_t index
) {
    return backends.at(index);
}

bool BackendPool::hasHealthyBackend() const {
    return healthyCount() > 0;
}

std::size_t BackendPool::healthyCount() const {
    std::size_t count = 0;

    for(const Backend& backend : backends) {
        if(backend.healthy) {
            ++count;
        }
    }

    return count;
}

void BackendPool::reset() {
    backends.clear();
}

const std::vector<Backend>& BackendPool::getBackends() const {
    return backends;
}