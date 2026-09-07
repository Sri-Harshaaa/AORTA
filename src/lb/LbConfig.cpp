#include "lb/LbConfig.hpp"
#include "lb/BalancingStrategy.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

bool parseSize(
    const std::string& text,
    std::size_t& value,
    std::string& error,
    const char* what
) {
    try {
        std::size_t consumed = 0;

        const unsigned long long parsed =
            std::stoull(text, &consumed);

        if(consumed != text.size()) {
            error = std::string("invalid value for ") + what + ": " + text;
            return false;
        }

        value = static_cast<std::size_t>(parsed);
        return true;

    } catch(...) {
        error = std::string("invalid value for ") + what + ": " + text;
        return false;
    }
}

bool parseInt(
    const std::string& text,
    int& value,
    std::string& error,
    const char* what
) {
    std::size_t parsed = 0;

    if(!parseSize(text, parsed, error, what)) {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

const char* env(const char* name) {
    const char* value = std::getenv(name);

    if(value == nullptr || value[0] == '\0') {
        return nullptr;
    }

    return value;
}

}


void LbConfig::printUsage(const char* program) {
    std::cout
        << "Usage: " << program << " [options]\n"
        << "\n"
        << "Options:\n"
        << "  --port <n>            Listen port (default 9000)\n"
        << "  --backends <path>     Backend list (default config/backends.conf)\n"
        << "  --strategy <name>     " << BalancingStrategy::validNames() << "\n"
        << "                        (default round_robin)\n"
        << "  --health-interval <n> Seconds between health checks (default 5)\n"
        << "  --max-connections <n> Client connection cap (default 100000)\n"
        << "  --max-buffer <bytes>  Per-direction relay cap (default 8388608)\n"
        << "  --max-total-buffer <bytes>\n"
        << "                        Process-wide relay budget (default 536870912)\n"
        << "  -h, --help            Show this message\n"
        << "\n"
        << "Environment: AORTA_LB_PORT, AORTA_LB_BACKENDS, AORTA_LB_STRATEGY\n";
}


bool LbConfig::parse(
    int argc,
    char* argv[],
    LbConfig& config,
    std::string& error
) {
    if(const char* value = env("AORTA_LB_PORT")) {
        if(!parseInt(value, config.listen_port, error, "AORTA_LB_PORT")) {
            return false;
        }
    }

    if(const char* value = env("AORTA_LB_BACKENDS")) {
        config.backends_path = value;
    }

    if(const char* value = env("AORTA_LB_STRATEGY")) {
        config.strategy = value;
    }

    for(int i = 1; i < argc; ++i) {

        const std::string argument(argv[i]);

        if(argument == "-h" || argument == "--help") {
            printUsage(argv[0]);
            std::exit(0);
        }

        const bool has_value = (i + 1) < argc;

        auto next = [&]() -> std::string {
            return has_value ? std::string(argv[++i]) : std::string();
        };

        if(argument == "--port") {
            if(!has_value) { error = "--port needs a value"; return false; }
            if(!parseInt(next(), config.listen_port, error, "--port")) return false;

        } else if(argument == "--backends") {
            if(!has_value) { error = "--backends needs a value"; return false; }
            config.backends_path = next();

        } else if(argument == "--strategy") {
            if(!has_value) { error = "--strategy needs a value"; return false; }
            config.strategy = next();

        } else if(argument == "--health-interval") {
            if(!has_value) { error = "--health-interval needs a value"; return false; }
            if(!parseInt(next(), config.health_interval, error, "--health-interval")) return false;

        } else if(argument == "--max-connections") {
            if(!has_value) { error = "--max-connections needs a value"; return false; }
            if(!parseSize(next(), config.max_connections, error, "--max-connections")) return false;

        } else if(argument == "--max-buffer") {
            if(!has_value) { error = "--max-buffer needs a value"; return false; }
            if(!parseSize(next(), config.max_connection_buffer, error, "--max-buffer")) return false;

        } else if(argument == "--max-total-buffer") {
            if(!has_value) { error = "--max-total-buffer needs a value"; return false; }
            if(!parseSize(next(), config.max_total_buffer, error, "--max-total-buffer")) return false;

        } else {
            error = "unknown argument: " + argument;
            return false;
        }
    }

    if(config.listen_port <= 0 || config.listen_port > 65535) {
        error = "port must be between 1 and 65535";
        return false;
    }

    if(!BalancingStrategy::isValid(config.strategy)) {
        error = "unknown strategy '" + config.strategy
            + "', expected one of: "
            + BalancingStrategy::validNames();
        return false;
    }

    if(config.health_interval <= 0) {
        error = "health interval must be positive";
        return false;
    }

    if(config.max_connections == 0) {
        error = "max connections must be positive";
        return false;
    }

    if(config.max_connection_buffer == 0) {
        error = "max buffer must be positive";
        return false;
    }

    if(config.max_total_buffer < config.max_connection_buffer) {
        error = "total buffer budget must be at least the per-connection cap";
        return false;
    }

    return true;
}


void LbConfig::print() const {
    std::cout
        << "AORTA load balancer\n"
        << "  port            " << listen_port << "\n"
        << "  strategy        " << strategy << "\n"
        << "  backends        " << backends_path << "\n"
        << "  health every    " << health_interval << "s\n"
        << "  max conns       " << max_connections << "\n"
        << "  buffer/conn     " << max_connection_buffer << " bytes\n"
        << "  buffer budget   " << max_total_buffer << " bytes\n"
        << std::flush;
}
