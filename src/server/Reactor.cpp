#include "server/Reactor.hpp"

#include "http/HttpResponse.hpp"
#include "server/Shutdown.hpp"
#include "task/TaskManager.hpp"

#include <csignal>
#include <iostream>
#include <cerrno>
#include <utility>
#include <vector>

#include <sys/socket.h>
#include <unistd.h>

Reactor::Reactor(
    int reactor_id,
    const ServerConfig& server_config,
    std::shared_ptr<MetricsRegistry> registry
) : id(reactor_id),
    config(server_config),
    task_store(
        std::make_shared<TaskManager>(
            server_config.redis_host,
            server_config.redis_port
        )
    ),
    http_handler(
        task_store,
        registry,
        server_config.public_directory
    ) {

    if(registry) {
        registry->registerSource(reactor_id, &metrics);
    }
}


void Reactor::drainCounters(Connection& connection) {
    const std::size_t read_bytes = connection.consumeBytesRead();
    const std::size_t written_bytes = connection.consumeBytesWritten();

    if(read_bytes > 0) {
        metrics.addBytesReceived(read_bytes);
    }

    if(written_bytes > 0) {
        metrics.addBytesSent(written_bytes);
    }
}

void Reactor::handleAccept() {
    while(true) {
        int client_fd = accept(
            listen_socket.getFd(),
            nullptr,
            nullptr
        );

        if(client_fd == -1) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }

            if(errno == EINTR) {
                continue;
            }

            return;
        }

        if(!Socket::setNonBlocking(client_fd)) {
            close(client_fd);
            continue;
        }

        if(!Socket::setNoDelay(client_fd)) {
            close(client_fd);
            continue;
        }

        if(!epoll.add(client_fd, EPOLLIN | EPOLLRDHUP)) {
            close(client_fd);
            continue;
        }

        connections[client_fd] = std::make_unique<Connection>(client_fd, id);

        metrics.incrementAccepted();
        metrics.incrementActiveConnections();

        refreshDeadline(
            client_fd,
            REQUEST_TIMEOUT_SECONDS
        );
    }
}

void Reactor::removeConnection(int fd) {
    epoll.remove(fd);

    connections.erase(fd);
    connection_states.erase(fd);

    metrics.decrementActiveConnections();
}

void Reactor::updateEvents(int fd, uint32_t events) {
    struct epoll_event event;

    event.events = events;
    event.data.fd = fd;

    if(epoll_ctl(
        epoll.getFd(),
        EPOLL_CTL_MOD,
        fd,
        &event
    ) == -1) {
        removeConnection(fd);
    }
}

void Reactor::refreshDeadline(int fd, int timeout_seconds) {
    ConnectionState state;

    state.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);

    connection_states[fd] = state;
}

void Reactor::removeExpiredConnections() {
    auto now = std::chrono::steady_clock::now();

    std::vector<int> expired_fds;

    for(const auto& entry : connection_states) {
        if(now >= entry.second.deadline) {
            expired_fds.push_back(entry.first);
        }
    }

    for(int fd : expired_fds) {
        if(connections.find(fd) == connections.end()) {
            continue;
        }

        removeConnection(fd);
    }
}

void Reactor::handleTimer() {
    timer.consume();

    metrics.tick();

    removeExpiredConnections();
}

void Reactor::handleClient(int fd) {
    auto it = connections.find(fd);

    if(it == connections.end()) {
        return;
    }

    Connection& connection = *(it->second);

    while(true) {
        Connection::ReadResult result = connection.read();

        if(result == Connection::ReadResult::Disconnected) {
            removeConnection(fd);
            return;
        }

        if(result == Connection::ReadResult::Error) {
            metrics.incrementErrors();
            removeConnection(fd);
            return;
        }

        if(result == Connection::ReadResult::BufferFull) {
            metrics.incrementErrors();
            removeConnection(fd);
            return;
        }

        if(result == Connection::ReadResult::WouldBlock) {
            break;
        }

        if(result == Connection::ReadResult::DataReceived) {
            refreshDeadline(
                fd,
                REQUEST_TIMEOUT_SECONDS
            );
        }

        while(true) {
            HttpParser::ParseResult parse_result = connection.parseRequest();

            if(parse_result == HttpParser::ParseResult::NeedMoreData) {
                connection.consumeParsedBytes();
                break;
            }

            if(parse_result == HttpParser::ParseResult::BadRequest) {
                metrics.incrementErrors();
                removeConnection(fd);
                return;
            }

            if(parse_result == HttpParser::ParseResult::RequestLineTooLarge) {
                metrics.incrementErrors();
                removeConnection(fd);
                return;
            }

            if(parse_result == HttpParser::ParseResult::HeadersTooLarge) {
                metrics.incrementErrors();
                removeConnection(fd);
                return;
            }

            if(parse_result == HttpParser::ParseResult::TooManyHeaders) {
                metrics.incrementErrors();
                removeConnection(fd);
                return;
            }

            if(parse_result == HttpParser::ParseResult::BodyTooLarge) {
                metrics.incrementErrors();
                removeConnection(fd);
                return;
            }

            if(parse_result == HttpParser::ParseResult::UnsupportedTransferEncoding) {
                metrics.incrementErrors();
                removeConnection(fd);
                return;
            }

            if(parse_result == HttpParser::ParseResult::UnsupportedVersion) {
                metrics.incrementErrors();
                removeConnection(fd);
                return;
            }

            if(parse_result != HttpParser::ParseResult::Complete) {
                metrics.incrementErrors();
                removeConnection(fd);
                return;
            }

            const HttpRequest& request = connection.getRequest();

            metrics.incrementRequests();

            HttpResponse response;

            http_handler.handle(
                request,
                response
            );

            if(request.hasHeaderToken("Connection", "close")) {
                connection.setCloseAfterWrite(true);
                response.setConnection("close");
            }

            std::string serialized_response = response.serialize();

            if(!connection.queueResponse(serialized_response)) {
                metrics.incrementErrors();
                removeConnection(fd);
                return;
            }

            if(response.hasFile() && response.getFileSize() > 0 && response.getFileSize() <= 16 * 1024 * 1024) {
                if(!connection.queueFile(
                    response.getFilePath(),
                    response.getFileSize()
                )) {
                    metrics.incrementErrors();
                    removeConnection(fd);
                    return;
                }
            }

            if(connection.hasRequestStarted()) {

                const auto elapsed =
                    std::chrono::steady_clock::now()
                    - connection.getRequestStart();

                metrics.recordLatency(
                    static_cast<std::uint64_t>(
                        std::chrono::duration_cast<
                            std::chrono::microseconds
                        >(elapsed).count()
                    )
                );
            }

            connection.consumeParsedBytes();
            connection.resetParser();

            refreshDeadline(
                fd,
                KEEP_ALIVE_TIMEOUT_SECONDS
            );

            if(connection.shouldCloseAfterWrite()) {
                break;
            }
        }

        if(connection.shouldCloseAfterWrite()) {
            break;
        }
    }

    if(connections.find(fd) == connections.end()) {
        return;
    }

    drainCounters(connection);

    if(connection.hasPendingOutput()) {
        updateEvents(
            fd,
            EPOLLIN | EPOLLOUT | EPOLLRDHUP
        );
    }
}

void Reactor::handleWrite(int fd) {
    auto it = connections.find(fd);

    if(it == connections.end()) {
        return;
    }

    Connection& connection = *(it->second);

    Connection::WriteResult result = connection.write();

    drainCounters(connection);

    if(result == Connection::WriteResult::Error) {
        metrics.incrementErrors();
        removeConnection(fd);
        return;
    }

    if(result == Connection::WriteResult::WouldBlock) {
        return;
    }

    if(connection.shouldCloseAfterWrite()) {
        metrics.incrementResponses();
        removeConnection(fd);
        return;
    }

    metrics.incrementResponses();

    updateEvents(
        fd,
        EPOLLIN | EPOLLRDHUP
    );
}

void Reactor::handleEvent(struct epoll_event& event) {
    int fd = event.data.fd;
    uint32_t events = event.events;

    if(fd == listen_socket.getFd()) {
        handleAccept();
        return;
    }

    if(fd == timer.getFd()) {
        handleTimer();
        return;
    }

    if(events & EPOLLERR) {
        metrics.incrementErrors();
        removeConnection(fd);
        return;
    }

    if(events & EPOLLHUP) {
        removeConnection(fd);
        return;
    }

    if(events & EPOLLIN) {
        handleClient(fd);
    }

    if(connections.find(fd) == connections.end()) {
        return;
    }

    if(events & EPOLLOUT) {
        handleWrite(fd);
    }

    if(connections.find(fd) == connections.end()) {
        return;
    }

    if(events & EPOLLRDHUP) {
        if(!connections.at(fd)->hasPendingOutput()) {
            removeConnection(fd);
        }
    }
}

void Reactor::run() {
    if(!listen_socket.bindAndListen(config.port, config.backlog)) {
        return;
    }

    if(!epoll.add(listen_socket.getFd(), EPOLLIN)) {
        return;
    }

    if(!timer.start(TIMER_INTERVAL_SECONDS)) {
        return;
    }

    if(!epoll.add(timer.getFd(), EPOLLIN)) {
        return;
    }

    while(!shutdown::isRequested()) {
        struct epoll_event events[1024];

        int ready = epoll.wait(events, 1024, -1);

        if(ready == -1) {
            if(errno == EINTR) {
                continue;
            }

            break;
        }

        for(int i = 0; i < ready; i++) {
            handleEvent(events[i]);
        }
    }
}

const Metrics& Reactor::getMetrics() const {
    return metrics;
}