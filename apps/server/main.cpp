#include "server/MetricsRegistry.hpp"
#include "server/Server.hpp"
#include "server/ServerConfig.hpp"
#include "server/ThreadedServer.hpp"

#include <iostream>
#include <memory>
#include <string>

int main(int argc, char* argv[]) {
    ServerConfig config;
    std::string error;

    if(!ServerConfig::parse(argc, argv, config, error)) {
        std::cerr << "aorta: " << error << "\n" << std::endl;
        ServerConfig::printUsage(argv[0]);
        return 1;
    }

    if(config.mode == ServerConfig::Mode::Threaded) {

        auto registry = std::make_shared<MetricsRegistry>();

        registry->setMode(
            ServerConfig::modeName(config.mode)
        );

        ThreadedServer server(config, registry);

        server.start();

        return 0;
    }

    Server server(config);

    server.start();

    return 0;
}
