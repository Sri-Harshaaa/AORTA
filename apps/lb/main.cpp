#include "lb/LoadBalancer.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

int main() {

    std::size_t reactor_count = 0;

    const char* environment =
        std::getenv("AORTA_LB_REACTORS");

    if(environment != nullptr) {

        try {
            reactor_count =
                std::stoull(environment);
        } catch(...) {
            reactor_count = 0;
        }
    }

    if(reactor_count == 0) {

        const unsigned int hardware =
            std::thread::hardware_concurrency();

        reactor_count =
            hardware == 0
            ? 1
            : hardware;
    }

    reactor_count =
        std::max<std::size_t>(
            1,
            reactor_count
        );

    std::cout
        << "Starting AORTA Load Balancer with "
        << reactor_count
        << " reactors"
        << std::endl;

    std::vector<std::thread> reactors;

    reactors.reserve(reactor_count);

    for(
        std::size_t i = 0;
        i < reactor_count;
        ++i
    ) {

        reactors.emplace_back(
            [i, reactor_count]() {

                LoadBalancer load_balancer(
                    static_cast<int>(i),
                    static_cast<int>(reactor_count)
                );

                load_balancer.start();
            }
        );
    }

    for(auto& reactor : reactors) {

        if(reactor.joinable()) {
            reactor.join();
        }
    }

    return 0;
}