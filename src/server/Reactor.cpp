#include "server/Reactor.hpp"

#include <cerrno>
#include <csignal>
#include <iostream>
#include <utility>
#include <vector>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

extern volatile std::sig_atomic_t shutdown_requested;

Reactor::Reactor(
    int id,
    WorkerPool& worker_pool
)
    : id(id),
      worker_pool(worker_pool),
      task_manager(worker_pool, static_cast<std::size_t>(id)),
      http_handler(&task_manager, metrics),
      completion_fd(
          worker_pool.getCompletionFd(
              static_cast<std::size_t>(id)
          )
      ) {
}


void Reactor::handleAccept() {
    while(true) {
        const int client_fd =
            accept(
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

        if(!epoll.add(
            client_fd,
            EPOLLIN | EPOLLRDHUP
        )) {
            close(client_fd);
            continue;
        }

        const std::uint64_t generation =
            ++next_connection_generation;

        connections[client_fd] =
            std::make_unique<Connection>(
                client_fd,
                id
            );

        metrics.incrementActiveConnections();

        connection_states[client_fd] = {
            std::chrono::steady_clock::now()
                + std::chrono::seconds(
                    REQUEST_TIMEOUT_SECONDS
                ),
            generation,
            false
        };
    }
}


void Reactor::removeConnection(
    int fd
) {
    auto iterator = connections.find(fd);

    if(iterator == connections.end()) {
        return;
    }

    epoll.remove(fd);
    connections.erase(iterator);
    connection_states.erase(fd);
    metrics.decrementActiveConnections();
}


void Reactor::updateEvents(
    int fd,
    uint32_t events
) {
    if(connections.find(fd) == connections.end()) {
        return;
    }

    if(!epoll.modify(fd, events)) {
        metrics.incrementErrors();
        removeConnection(fd);
    }
}


void Reactor::refreshDeadline(
    int fd,
    int timeout_seconds
) {
    auto iterator = connection_states.find(fd);

    if(iterator == connection_states.end()) {
        return;
    }

    iterator->second.deadline =
        std::chrono::steady_clock::now()
        + std::chrono::seconds(timeout_seconds);
}


void Reactor::removeExpiredConnections() {
    const auto now =
        std::chrono::steady_clock::now();

    std::vector<int> expired_fds;
    expired_fds.reserve(connection_states.size());

    for(const auto& entry : connection_states) {
        if(now >= entry.second.deadline) {
            expired_fds.push_back(entry.first);
        }
    }

    for(const int fd : expired_fds) {
        if(connections.find(fd) == connections.end()) {
            continue;
        }

        removeConnection(fd);
    }
}


void Reactor::handleTimer() {
    timer.consume();
    removeExpiredConnections();
}


void Reactor::handleClient(
    int fd
) {
    auto iterator = connections.find(fd);

    if(iterator == connections.end()) {
        return;
    }

    auto state_iterator =
        connection_states.find(fd);

    if(state_iterator == connection_states.end()) {
        removeConnection(fd);
        return;
    }

    ConnectionState& state =
        state_iterator->second;

    if(state.redis_pending) {
        return;
    }

    Connection& connection =
        *iterator->second;

    while(true) {
        const Connection::ReadResult result =
            connection.read();

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
            const HttpParser::ParseResult parse_result =
                connection.parseRequest();

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

            const HttpRequest request =
                connection.getRequest();

            metrics.incrementRequests();

            const bool close_after_write =
                request.hasHeaderToken(
                    "Connection",
                    "close"
                );

            const std::uint64_t generation =
                state.generation;

            HttpResponse response;

            const HttpHandler::HandleResult result =
                http_handler.handle(
                    request,
                    response,
                    [this, fd, generation, close_after_write](HttpResponse completed_response) {
                        completeAsyncResponse(
                            fd,
                            generation,
                            close_after_write,
                            std::move(completed_response)
                        );
                    }
                );

            if(result == HttpHandler::HandleResult::Pending) {
                connection.consumeParsedBytes();
                connection.resetParser();

                state.redis_pending = true;

                refreshDeadline(
                    fd,
                    REQUEST_TIMEOUT_SECONDS
                );

                updateEvents(
                    fd,
                    EPOLLRDHUP
                );

                return;
            }

            if(close_after_write) {
                connection.setCloseAfterWrite(true);
                response.setConnection("close");
            }

            if(!connection.queueResponse(
                response.serialize()
            )) {
                metrics.incrementErrors();
                removeConnection(fd);
                return;
            }

            if(
                response.hasFile()
                &&
                response.getFileSize() > 0
                &&
                response.getFileSize() <= 16 * 1024 * 1024
            ) {
                if(!connection.queueFile(
                    response.getFilePath(),
                    response.getFileSize()
                )) {
                    metrics.incrementErrors();
                    removeConnection(fd);
                    return;
                }
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

    if(connection.hasPendingOutput()) {
        updateEvents(
            fd,
            EPOLLIN | EPOLLOUT | EPOLLRDHUP
        );
    }
}


void Reactor::handleWrite(
    int fd
) {
    auto iterator = connections.find(fd);

    if(iterator == connections.end()) {
        return;
    }

    Connection& connection =
        *iterator->second;

    const Connection::WriteResult result =
        connection.write();

    if(result == Connection::WriteResult::Error) {
        metrics.incrementErrors();
        removeConnection(fd);
        return;
    }

    if(result == Connection::WriteResult::WouldBlock) {
        return;
    }

    metrics.incrementResponses();

    if(connection.shouldCloseAfterWrite()) {
        removeConnection(fd);
        return;
    }

    updateEvents(
        fd,
        EPOLLIN | EPOLLRDHUP
    );
}


void Reactor::handleCompletions() {
    worker_pool.consumeCompletionEvent(
        static_cast<std::size_t>(id)
    );

    while(true) {
        WorkerPool::Completion completion;

        if(!worker_pool.popCompletion(
            static_cast<std::size_t>(id),
            completion
        )) {
            break;
        }

        if(completion) {
            completion();
        }
    }
}


void Reactor::completeAsyncResponse(
    int fd,
    std::uint64_t generation,
    bool close_after_write,
    HttpResponse response
) {
    auto connection_iterator =
        connections.find(fd);

    if(connection_iterator == connections.end()) {
        return;
    }

    auto state_iterator =
        connection_states.find(fd);

    if(state_iterator == connection_states.end()) {
        return;
    }

    ConnectionState& state =
        state_iterator->second;

    if(!isGenerationCurrent(state, generation)) {
        return;
    }

    if(!state.redis_pending) {
        return;
    }

    Connection& connection =
        *connection_iterator->second;

    state.redis_pending = false;

    if(close_after_write) {
        connection.setCloseAfterWrite(true);
        response.setConnection("close");
    }

    if(response.hasFile()) {
        metrics.incrementErrors();
        removeConnection(fd);
        return;
    }

    if(!connection.queueResponse(
        response.serialize()
    )) {
        metrics.incrementErrors();
        removeConnection(fd);
        return;
    }

    refreshDeadline(
        fd,
        KEEP_ALIVE_TIMEOUT_SECONDS
    );

    updateEvents(
        fd,
        EPOLLIN | EPOLLOUT | EPOLLRDHUP
    );
}


bool Reactor::isGenerationCurrent(
    const ConnectionState& state,
    std::uint64_t generation
) {
    return state.generation == generation;
}


void Reactor::handleEvent(
    struct epoll_event& event
) {
    const int fd = event.data.fd;
    const uint32_t events = event.events;

    if(fd == listen_socket.getFd()) {
        handleAccept();
        return;
    }

    if(fd == timer.getFd()) {
        handleTimer();
        return;
    }

    if(fd == completion_fd) {
        handleCompletions();
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


void Reactor::run(
    int port
) {
    if(!listen_socket.bindAndListen(port)) {
        return;
    }

    if(!epoll.add(
        listen_socket.getFd(),
        EPOLLIN
    )) {
        return;
    }

    if(!timer.start(
        TIMER_INTERVAL_SECONDS
    )) {
        return;
    }

    if(!epoll.add(
        timer.getFd(),
        EPOLLIN
    )) {
        return;
    }

    if(completion_fd == -1) {
        std::cerr
            << "Reactor "
            << id
            << ": worker completion eventfd unavailable"
            << std::endl;

        return;
    }

    if(!epoll.add(
        completion_fd,
        EPOLLIN
    )) {
        return;
    }

    std::cout
        << "Reactor "
        << id
        << " started on port "
        << port
        << std::endl;

    while(shutdown_requested == 0) {
        struct epoll_event events[1024];

        const int ready =
            epoll.wait(
                events,
                1024,
                -1
            );

        if(ready == -1) {
            if(errno == EINTR) {
                continue;
            }

            break;
        }

        for(int i = 0; i < ready; ++i) {
            handleEvent(events[i]);
        }
    }
}


const Metrics& Reactor::getMetrics() const {
    return metrics;
}
