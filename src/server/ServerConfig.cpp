#include "server/ServerConfig.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

bool parseInt(
    const std::string& text,
    int& value,
    std::string& error,
    const char* what
) {
    try {
        std::size_t consumed = 0;

        const int parsed = std::stoi(text, &consumed);

        if(consumed != text.size()) {
            error = std::string("invalid value for ") + what + ": " + text;
            return false;
        }

        value = parsed;
        return true;

    } catch(...) {
        error = std::string("invalid value for ") + what + ": " + text;
        return false;
    }
}

const char* env(const char* name) {
    const char* value = std::getenv(name);

    if(value == nullptr || value[0] == '\0') {
        return nullptr;
    }

    return value;
}

}


const char* ServerConfig::modeName(Mode mode) {
    return mode == Mode::Threaded ? "threaded" : "epoll";
}


void ServerConfig::printUsage(const char* program) {
    std::cout
        << "Usage: " << program << " [port] [options]\n"
        << "\n"
        << "Options:\n"
        << "  --port <n>          Listen port (default 8080)\n"
        << "  --mode <name>       epoll | threaded (default epoll)\n"
        << "  --workers <n>       Reactor count in epoll mode (default: CPU count)\n"
        << "  --max-threads <n>   Connection cap in threaded mode (default 4096)\n"
        << "  --backlog <n>       listen() backlog (default 4096)\n"
        << "  --redis-host <h>    Redis host (default redis)\n"
        << "  --redis-port <n>    Redis port (default 6379)\n"
        << "  --redis-pool <n>    Redis connections in threaded mode (default 32)\n"
        << "  --public <dir>      Static file root (default ./public)\n"
        << "  -h, --help          Show this message\n"
        << "\n"
        << "Environment: AORTA_PORT, AORTA_MODE, AORTA_WORKERS,\n"
        << "             AORTA_REDIS_HOST, AORTA_REDIS_PORT, AORTA_REDIS_POOL\n";
}


bool ServerConfig::parse(
    int argc,
    char* argv[],
    ServerConfig& config,
    std::string& error
) {
    if(const char* value = env("AORTA_PORT")) {
        if(!parseInt(value, config.port, error, "AORTA_PORT")) {
            return false;
        }
    }

    if(const char* value = env("AORTA_WORKERS")) {
        if(!parseInt(value, config.workers, error, "AORTA_WORKERS")) {
            return false;
        }
    }

    if(const char* value = env("AORTA_MODE")) {
        const std::string mode(value);

        if(mode == "threaded") {
            config.mode = Mode::Threaded;
        } else if(mode == "epoll") {
            config.mode = Mode::Epoll;
        } else {
            error = "AORTA_MODE must be epoll or threaded";
            return false;
        }
    }

    if(const char* value = env("AORTA_REDIS_HOST")) {
        config.redis_host = value;
    }

    if(const char* value = env("AORTA_REDIS_PORT")) {
        if(!parseInt(value, config.redis_port, error, "AORTA_REDIS_PORT")) {
            return false;
        }
    }

    if(const char* value = env("AORTA_REDIS_POOL")) {
        int pool = 0;

        if(!parseInt(value, pool, error, "AORTA_REDIS_POOL")) {
            return false;
        }

        config.redis_pool_size = static_cast<std::size_t>(pool < 0 ? 0 : pool);
    }

    for(int i = 1; i < argc; ++i) {

        const std::string argument(argv[i]);

        if(argument == "-h" || argument == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        }

        /*
         * A bare number as the first argument keeps the original
         * "aorta 8081" invocation working, which docker-compose uses.
         */
        if(i == 1 && !argument.empty() && argument[0] != '-') {
            if(!parseInt(argument, config.port, error, "port")) {
                return false;
            }
            continue;
        }

        const bool has_value = (i + 1) < argc;

        auto next = [&]() -> std::string {
            return has_value ? std::string(argv[++i]) : std::string();
        };

        if(argument == "--port") {
            if(!has_value) { error = "--port needs a value"; return false; }
            if(!parseInt(next(), config.port, error, "--port")) return false;

        } else if(argument == "--mode") {
            if(!has_value) { error = "--mode needs a value"; return false; }

            const std::string mode = next();

            if(mode == "threaded") {
                config.mode = Mode::Threaded;
            } else if(mode == "epoll") {
                config.mode = Mode::Epoll;
            } else {
                error = "--mode must be epoll or threaded";
                return false;
            }

        } else if(argument == "--workers") {
            if(!has_value) { error = "--workers needs a value"; return false; }
            if(!parseInt(next(), config.workers, error, "--workers")) return false;

        } else if(argument == "--max-threads") {
            if(!has_value) { error = "--max-threads needs a value"; return false; }
            if(!parseInt(next(), config.max_threads, error, "--max-threads")) return false;

        } else if(argument == "--backlog") {
            if(!has_value) { error = "--backlog needs a value"; return false; }
            if(!parseInt(next(), config.backlog, error, "--backlog")) return false;

        } else if(argument == "--redis-host") {
            if(!has_value) { error = "--redis-host needs a value"; return false; }
            config.redis_host = next();

        } else if(argument == "--redis-port") {
            if(!has_value) { error = "--redis-port needs a value"; return false; }
            if(!parseInt(next(), config.redis_port, error, "--redis-port")) return false;

        } else if(argument == "--redis-pool") {
            if(!has_value) { error = "--redis-pool needs a value"; return false; }

            int pool = 0;
            if(!parseInt(next(), pool, error, "--redis-pool")) return false;

            config.redis_pool_size = static_cast<std::size_t>(pool < 0 ? 0 : pool);

        } else if(argument == "--public") {
            if(!has_value) { error = "--public needs a value"; return false; }
            config.public_directory = next();

        } else {
            error = "unknown argument: " + argument;
            return false;
        }
    }

    if(config.port <= 0 || config.port > 65535) {
        error = "port must be between 1 and 65535";
        return false;
    }

    if(config.redis_port <= 0 || config.redis_port > 65535) {
        error = "redis port must be between 1 and 65535";
        return false;
    }

    if(config.workers < 0) {
        error = "workers cannot be negative";
        return false;
    }

    if(config.max_threads <= 0) {
        error = "max threads must be positive";
        return false;
    }

    if(config.backlog <= 0) {
        error = "backlog must be positive";
        return false;
    }

    return true;
}


void ServerConfig::print() const {
    std::cout
        << "AORTA server\n"
        << "  mode          " << modeName(mode) << "\n"
        << "  port          " << port << "\n";

    if(mode == Mode::Epoll) {
        std::cout << "  workers       " << workers << "\n";
    } else {
        std::cout << "  max threads   " << max_threads << "\n";
        std::cout << "  redis pool    " << redis_pool_size << "\n";
    }

    std::cout
        << "  backlog       " << backlog << "\n"
        << "  redis         " << redis_host << ":" << redis_port << "\n"
        << "  public dir    " << public_directory << "\n"
        << std::flush;
}
