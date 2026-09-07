#include "lb/LbConfig.hpp"
#include "lb/LoadBalancer.hpp"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    LbConfig config;
    std::string error;

    if(!LbConfig::parse(argc, argv, config, error)) {
        std::cerr << "aorta_lb: " << error << "\n" << std::endl;
        LbConfig::printUsage(argv[0]);
        return 1;
    }

    LoadBalancer load_balancer(config);

    load_balancer.start();

    return 0;
}
