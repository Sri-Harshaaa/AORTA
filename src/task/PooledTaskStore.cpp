#include "task/PooledTaskStore.hpp"

PooledTaskStore::PooledTaskStore(
    const std::string& redis_host,
    int redis_port,
    std::size_t pool_size
) {
    if(pool_size == 0) {
        pool_size = 1;
    }

    clients.reserve(pool_size);
    free_list.reserve(pool_size);

    for(std::size_t i = 0; i < pool_size; ++i) {

        clients.push_back(
            std::make_unique<TaskManager>(
                redis_host,
                redis_port
            )
        );

        free_list.push_back(i);
    }
}


std::size_t PooledTaskStore::acquire() {
    std::unique_lock<std::mutex> lock(mutex);

    available.wait(
        lock,
        [this]() {
            return !free_list.empty();
        }
    );

    const std::size_t index = free_list.back();

    free_list.pop_back();

    return index;
}


void PooledTaskStore::release(std::size_t index) {
    {
        std::lock_guard<std::mutex> lock(mutex);

        free_list.push_back(index);
    }

    available.notify_one();
}


PooledTaskStore::Lease::Lease(PooledTaskStore& pool)
    : owner(pool),
      index(pool.acquire()) {
}


PooledTaskStore::Lease::~Lease() {
    owner.release(index);
}


TaskManager& PooledTaskStore::Lease::get() {
    return *owner.clients[index];
}


std::size_t PooledTaskStore::size() const {
    std::lock_guard<std::mutex> lock(mutex);

    return clients.size();
}


std::vector<Task> PooledTaskStore::getAll() {
    Lease lease(*this);

    return lease.get().getAll();
}


bool PooledTaskStore::create(
    const std::string& title,
    Task& created_task
) {
    Lease lease(*this);

    return lease.get().create(title, created_task);
}


bool PooledTaskStore::update(
    std::size_t id,
    const std::string& title,
    bool completed,
    Task& updated_task
) {
    Lease lease(*this);

    return lease.get().update(
        id,
        title,
        completed,
        updated_task
    );
}


bool PooledTaskStore::remove(
    std::size_t id
) {
    Lease lease(*this);

    return lease.get().remove(id);
}
