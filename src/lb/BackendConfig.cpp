#include "lb/BackendConfig.hpp"

#include <fstream>
#include <sstream>

std::vector<Backend> BackendConfig::load(
    const std::string& path
) {
    std::vector<Backend> backends;

    std::ifstream file(path);

    if(!file.is_open()) {
        return backends;
    }

    std::string line;

    while(std::getline(file, line)) {

        if(line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream stream(line);

        std::string host;
        std::string port_string;

        if(!std::getline(stream, host, ':')) {
            continue;
        }

        if(!std::getline(stream, port_string)) {
            continue;
        }

        try {
            int port = std::stoi(port_string);

            if(port <= 0 || port > 65535) {
                continue;
            }

            backends.push_back({
                host,
                port,
                true,
                0,
                0
            });

        } catch(...) {
            continue;
        }
    }

    return backends;
}