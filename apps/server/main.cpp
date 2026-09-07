#include "server/Server.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[]) {

    int port = 8080;

    if(argc > 2) {
        std::cerr << "Usage: " << argv[0] << " [port]" << std::endl;
        return 1;
    }

    if(argc == 2) {
        port = std::atoi(argv[1]);

        if(port <= 0 || port > 65535) {
            std::cerr << "Invalid port: " << argv[1] << std::endl;
            return 1;
        }
    }

    Server server(port);
    server.start();

    return 0;
}