#pragma once

#include <cstddef>
#include <string>

/*
 * Runtime configuration for the backend server.
 *
 * Command line flags win over environment variables, which win over the
 * defaults here. The environment path exists so docker-compose can configure a
 * container without rewriting the command.
 */
struct ServerConfig {

    enum class Mode {
        Epoll,
        Threaded
    };

    int port{8080};

    Mode mode{Mode::Epoll};

    // 0 means one reactor per hardware thread.
    int workers{0};

    std::string redis_host{"redis"};
    int redis_port{6379};

    // 0 means match the worker count (epoll) or use 32 (threaded).
    std::size_t redis_pool_size{0};

    /*
     * Thread-per-connection cap. Past this the server accepts and immediately
     * refuses, which is what a real thread-per-connection server does when it
     * runs out of stack address space, only visible instead of fatal.
     */
    int max_threads{4096};

    int backlog{4096};

    std::string public_directory{"./public"};

    static const char* modeName(Mode mode);

    static bool parse(
        int argc,
        char* argv[],
        ServerConfig& config,
        std::string& error
    );

    static void printUsage(const char* program);

    void print() const;
};
