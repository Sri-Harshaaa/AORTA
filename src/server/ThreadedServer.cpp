#include "server/ThreadedServer.hpp"

#include "http/HttpParser.hpp"
#include "http/HttpResponse.hpp"
#include "server/Shutdown.hpp"
#include "task/PooledTaskStore.hpp"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::size_t READ_CHUNK = 4096;

std::size_t defaultPoolSize(const ServerConfig& config) {
    if(config.redis_pool_size > 0) {
        return config.redis_pool_size;
    }

    return 32;
}

}


ThreadedServer::ThreadedServer(
    const ServerConfig& server_config,
    std::shared_ptr<MetricsRegistry> metrics_registry
) : config(server_config),
    registry(std::move(metrics_registry)),
    task_store(
        std::make_shared<PooledTaskStore>(
            server_config.redis_host,
            server_config.redis_port,
            defaultPoolSize(server_config)
        )
    ),
    http_handler(
        task_store,
        registry,
        server_config.public_directory
    ),
    listen_socket(false) {

    if(registry) {
        registry->registerSource(0, &metrics);
    }
}


bool ThreadedServer::sendAll(
    int fd,
    const char* data,
    std::size_t size
) {
    std::size_t offset = 0;

    while(offset < size) {

        const ssize_t sent = send(
            fd,
            data + offset,
            size - offset,
            MSG_NOSIGNAL
        );

        if(sent > 0) {
            offset += static_cast<std::size_t>(sent);
            continue;
        }

        if(sent == -1 && errno == EINTR) {
            continue;
        }

        return false;
    }

    metrics.addBytesSent(size);

    return true;
}


void ThreadedServer::refuse(int client_fd) {
    static const char response[] =
        "HTTP/1.1 503 Service Unavailable\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 19\r\n"
        "Connection: close\r\n"
        "\r\n"
        "Service Unavailable";

    send(
        client_fd,
        response,
        sizeof(response) - 1,
        MSG_NOSIGNAL
    );

    close(client_fd);

    metrics.incrementRejected();
}


void ThreadedServer::handleConnection(int client_fd) {
    Socket::setNoDelay(client_fd);
    Socket::setReceiveTimeout(client_fd, RECEIVE_TIMEOUT_SECONDS);
    Socket::setSendTimeout(client_fd, SEND_TIMEOUT_SECONDS);

    metrics.incrementAccepted();
    metrics.incrementActiveConnections();

    HttpParser parser;

    std::string buffer;
    std::vector<char> chunk(READ_CHUNK);

    bool keep_going = true;

    std::chrono::steady_clock::time_point request_start{};
    bool request_started = false;

    while(keep_going && !shutdown::isRequested()) {

        const ssize_t received = recv(
            client_fd,
            chunk.data(),
            chunk.size(),
            0
        );

        if(received == 0) {
            break;
        }

        if(received == -1) {

            if(errno == EINTR) {
                continue;
            }

            /*
             * With SO_RCVTIMEO set, EAGAIN means the client went idle past the
             * keep-alive window rather than that the socket would block.
             */
            break;
        }

        if(!request_started) {
            request_start = std::chrono::steady_clock::now();
            request_started = true;
        }

        metrics.addBytesReceived(static_cast<std::size_t>(received));

        buffer.append(chunk.data(), static_cast<std::size_t>(received));

        if(buffer.size() > 1024 * 1024) {
            metrics.incrementErrors();
            break;
        }

        while(true) {

            const HttpParser::ParseResult result = parser.parse(buffer);

            if(result == HttpParser::ParseResult::NeedMoreData) {

                const std::size_t consumed = parser.consumeParsedBytes();

                if(consumed > 0) {
                    buffer.erase(0, consumed);
                }

                break;
            }

            if(result != HttpParser::ParseResult::Complete) {
                metrics.incrementErrors();
                keep_going = false;
                break;
            }

            const HttpRequest& request = parser.getRequest();

            metrics.incrementRequests();

            HttpResponse response;

            http_handler.handle(request, response);

            const bool close_requested =
                request.hasHeaderToken("Connection", "close");

            if(close_requested) {
                response.setConnection("close");
            }

            const std::string head = response.serialize();

            bool sent = sendAll(client_fd, head.data(), head.size());

            /*
             * A file response serializes to headers only. The epoll path
             * streams the body with sendfile(); here a plain blocking read and
             * write is both simpler and truer to the model being measured.
             */
            if(sent && response.hasFile() && response.getFileSize() > 0) {

                std::ifstream file(
                    response.getFilePath(),
                    std::ios::binary
                );

                if(file.is_open()) {

                    std::vector<char> file_chunk(64 * 1024);

                    std::size_t remaining = response.getFileSize();

                    while(remaining > 0 && sent) {

                        const std::size_t want =
                            remaining < file_chunk.size()
                                ? remaining
                                : file_chunk.size();

                        file.read(file_chunk.data(),
                                  static_cast<std::streamsize>(want));

                        const std::streamsize got = file.gcount();

                        if(got <= 0) {
                            break;
                        }

                        sent = sendAll(
                            client_fd,
                            file_chunk.data(),
                            static_cast<std::size_t>(got)
                        );

                        remaining -= static_cast<std::size_t>(got);
                    }

                } else {
                    sent = false;
                }
            }

            if(request_started) {

                const auto elapsed =
                    std::chrono::steady_clock::now() - request_start;

                metrics.recordLatency(
                    static_cast<std::uint64_t>(
                        std::chrono::duration_cast<
                            std::chrono::microseconds
                        >(elapsed).count()
                    )
                );

                request_started = false;
            }

            if(!sent) {
                metrics.incrementErrors();
                keep_going = false;
                break;
            }

            metrics.incrementResponses();

            const std::size_t consumed = parser.consumeParsedBytes();

            if(consumed > 0) {
                buffer.erase(0, consumed);
            }

            parser.reset();

            if(close_requested) {
                keep_going = false;
                break;
            }

            /*
             * A completed request must have consumed something. If it somehow
             * did not, looping again would re-parse the same bytes forever and
             * pin this thread, so bail instead.
             */
            if(consumed == 0) {
                keep_going = false;
                break;
            }

            if(buffer.empty()) {
                break;
            }
        }
    }

    close(client_fd);

    metrics.decrementActiveConnections();

    active_threads.fetch_sub(1, std::memory_order_relaxed);
}


void ThreadedServer::start() {
    shutdown::install();

    config.print();

    if(!listen_socket.bindAndListen(config.port, config.backlog)) {
        std::cerr << "Threaded server listen failed" << std::endl;
        return;
    }

    /*
     * accept() honours SO_RCVTIMEO, so the loop wakes once a second to notice
     * a shutdown request. Relying on EINTR would not work here because glibc
     * installs signal handlers with SA_RESTART.
     */
    Socket::setReceiveTimeout(
        listen_socket.getFd(),
        ACCEPT_TIMEOUT_SECONDS
    );

    std::cout
        << "Listening on port "
        << config.port
        << " with up to "
        << config.max_threads
        << " connection threads"
        << std::endl;

    while(!shutdown::isRequested()) {

        const int client_fd = accept(
            listen_socket.getFd(),
            nullptr,
            nullptr
        );

        if(client_fd == -1) {

            if(
                errno == EAGAIN ||
                errno == EWOULDBLOCK ||
                errno == EINTR
            ) {
                continue;
            }

            if(
                errno == EMFILE ||
                errno == ENFILE
            ) {
                metrics.incrementRejected();

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(10)
                );

                continue;
            }

            break;
        }

        const int in_flight =
            active_threads.fetch_add(1, std::memory_order_relaxed) + 1;

        if(in_flight > config.max_threads) {
            active_threads.fetch_sub(1, std::memory_order_relaxed);
            refuse(client_fd);
            continue;
        }

        try {

            std::thread(
                [this, client_fd]() {
                    handleConnection(client_fd);
                }
            ).detach();

        } catch(const std::system_error&) {

            /*
             * The process hit its thread limit. That is the headline result
             * for this model, so record it rather than letting it terminate.
             */
            active_threads.fetch_sub(1, std::memory_order_relaxed);
            refuse(client_fd);
        }
    }

    std::cout << "\nShutdown requested" << std::endl;

    /*
     * Detached threads own their sockets and exit on their own read timeout.
     * Wait a bounded moment so most finish before the process image goes away.
     */
    for(int i = 0; i < 50; ++i) {

        if(active_threads.load(std::memory_order_relaxed) == 0) {
            break;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(100)
        );
    }

    std::cout
        << "Threaded server stopped with "
        << active_threads.load(std::memory_order_relaxed)
        << " threads still draining"
        << std::endl;
}
