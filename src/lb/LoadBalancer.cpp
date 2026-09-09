#include "lb/LoadBalancer.hpp"
#include "lb/BackendConfig.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

std::atomic<bool> LoadBalancer::shutdown_requested{false};

void LoadBalancer::handleSignal(int signal) {
    if(signal == SIGINT || signal == SIGTERM) {
        shutdown_requested.store(true);
    }
}

LoadBalancer::RoutingMode LoadBalancer::loadRoutingMode() {
    const char* environment =
        std::getenv("AORTA_LB_ROUTING");

    if(environment == nullptr) {
        return RoutingMode::RoundRobin;
    }

    const std::string mode(environment);

    if(
        mode == "consistent_hash" ||
        mode == "consistent-hash" ||
        mode == "hash"
    ) {
        return RoutingMode::ConsistentHash;
    }

    return RoutingMode::RoundRobin;
}

LoadBalancer::LoadBalancer(
    int reactor_id,
    int reactor_count
)
    : reactor_id(reactor_id),
      reactor_count(reactor_count),
      routing_mode(loadRoutingMode()) {

    const std::vector<Backend> configured_backends =
        BackendConfig::load("config/backends.conf");

    for(const Backend& backend : configured_backends) {
        backend_pool.addBackend(backend);
    }

    consistent_hash.build(backend_pool);

    std::cout
        << "LB Reactor "
        << reactor_id
        << "/"
        << reactor_count
        << " loaded "
        << backend_pool.size()
        << " backends"
        << std::endl;

    std::cout
        << "LB Reactor "
        << reactor_id
        << " routing mode: "
        << (
            routing_mode == RoutingMode::RoundRobin
            ? "round_robin"
            : "consistent_hash"
        )
        << std::endl;

    std::cout
        << "LB Reactor "
        << reactor_id
        << " consistent hash ring contains "
        << consistent_hash.size()
        << " virtual nodes"
        << std::endl;
}

void LoadBalancer::start() {
    std::signal(SIGINT, LoadBalancer::handleSignal);
    std::signal(SIGTERM, LoadBalancer::handleSignal);

    if(!listen_socket.bindAndListen(LISTEN_PORT)) {
        std::cerr
            << "Load Balancer Reactor "
            << reactor_id
            << " listen failed"
            << std::endl;
        return;
    }

    if(!epoll.add(
        listen_socket.getFd(),
        EPOLLIN
    )) {
        std::cerr
            << "LB Reactor "
            << reactor_id
            << " failed to add listen socket"
            << std::endl;
        return;
    }

    if(!health_timer.start(
        HEALTH_CHECK_INTERVAL
    )) {
        std::cerr
            << "LB Reactor "
            << reactor_id
            << " failed to start health timer"
            << std::endl;
        return;
    }

    if(!epoll.add(
        health_timer.getFd(),
        EPOLLIN
    )) {
        std::cerr
            << "LB Reactor "
            << reactor_id
            << " failed to add health timer"
            << std::endl;
        return;
    }

    std::cout
        << "LB Reactor "
        << reactor_id
        << " listening on port "
        << LISTEN_PORT
        << std::endl;

    struct epoll_event events[1024];

    while(!shutdown_requested.load()) {

        health_checker.expire(epoll);

        int timeout = 1000;

        const int health_timeout =
            health_checker.nextTimeoutMs();

        if(health_timeout >= 0) {
            timeout = std::min(
                timeout,
                health_timeout
            );
        }

        const int event_count =
            epoll.wait(
                events,
                1024,
                timeout
            );

        if(event_count == -1) {

            if(errno == EINTR) {
                continue;
            }

            std::cerr
                << "LB Reactor "
                << reactor_id
                << " epoll_wait() failed"
                << std::endl;

            break;
        }

        for(int i = 0; i < event_count; ++i) {

            const int fd =
                events[i].data.fd;

            const uint32_t event_flags =
                events[i].events;

            if(fd == listen_socket.getFd()) {
                handleAccept();
                continue;
            }

            if(fd == health_timer.getFd()) {
                handleHealthCheck();
                continue;
            }

            if(health_checker.handles(fd)) {
                handleHealthEvent(
                    fd,
                    event_flags
                );
                continue;
            }

            bool is_backend_fd = false;

            auto backend_mapping =
                backend_to_client.find(fd);

            if(
                backend_mapping !=
                backend_to_client.end()
            ) {
                is_backend_fd = true;
            }

            int client_fd = fd;

            if(is_backend_fd) {
                client_fd =
                    backend_mapping->second;
            }

            auto connection_it =
                connections.find(client_fd);

            if(
                connection_it ==
                connections.end()
            ) {
                continue;
            }

            ConnectionPair& connection =
                connection_it->second;

            if(event_flags & EPOLLERR) {

                if(is_backend_fd && connection.backend_connecting) {

                    failoverConnection(
                        connection.client_fd,
                        connection.backend_index
                    );

                    continue;
                }

                closeConnection(
                    connection.client_fd
                );

                continue;
            }

            if(event_flags & EPOLLHUP) {

                if(is_backend_fd && connection.backend_connecting) {

                    failoverConnection(
                        connection.client_fd,
                        connection.backend_index
                    );

                    continue;
                }

                closeConnection(
                    connection.client_fd
                );

                continue;
            }

            if(
                (event_flags & EPOLLRDHUP) &&
                !is_backend_fd
            ) {

                connection.client_read_closed = true;

                if(connection.backend_fd != -1) {
                    shutdown(
                        connection.backend_fd,
                        SHUT_WR
                    );
                }
            }

            auto current_it =
                connections.find(client_fd);

            if(
                current_it ==
                connections.end()
            ) {
                continue;
            }

            ConnectionPair& current =
                current_it->second;

            if(event_flags & EPOLLIN) {

                if(is_backend_fd) {

                    forwardData(
                        current.backend_fd,
                        current.backend_to_client,
                        current.backend_to_client_offset
                    );

                } else {

                    if(
                        !current.metrics_connection &&
                        current.backend_fd == -1
                    ) {

                        char initial_buffer[1024];

                        const ssize_t peeked =
                            recv(
                                current.client_fd,
                                initial_buffer,
                                sizeof(initial_buffer),
                                MSG_PEEK
                            );

                        if(peeked > 0) {

                            const std::string request(
                                initial_buffer,
                                static_cast<std::size_t>(
                                    peeked
                                )
                            );

                            static constexpr char METRICS_PREFIX[] =
                                "GET /metrics ";

                            constexpr std::size_t METRICS_PREFIX_SIZE =
                                sizeof(METRICS_PREFIX) - 1;

                            if(
                                static_cast<std::size_t>(peeked) <
                                METRICS_PREFIX_SIZE &&
                                std::string_view(
                                    METRICS_PREFIX,
                                    static_cast<std::size_t>(peeked)
                                ) == request
                            ) {
                                continue;
                            }

                            if(
                                static_cast<std::size_t>(peeked) >=
                                METRICS_PREFIX_SIZE &&
                                request.rfind(
                                    METRICS_PREFIX,
                                    0
                                ) == 0
                            ) {
                                current.metrics_connection =
                                    true;
                            }

                        } else if(
                            peeked == -1 &&
                            errno != EINTR &&
                            errno != EAGAIN &&
                            errno != EWOULDBLOCK
                        ) {

                            closeConnection(
                                current.client_fd
                            );

                            continue;
                        }

                        if(!current.metrics_connection) {

                            if(!connectClientToBackend(
                                current.client_fd
                            )) {

                                closeConnection(
                                    current.client_fd
                                );

                                continue;
                            }
                        }
                    }

                    if(current.metrics_connection) {

                        char buffer[4096];

                        const ssize_t received =
                            recv(
                                current.client_fd,
                                buffer,
                                sizeof(buffer) - 1,
                                0
                            );

                        if(received <= 0) {
                            closeConnection(
                                current.client_fd
                            );
                            continue;
                        }

                        buffer[received] = '\0';

                        const std::string request(
                            buffer,
                            static_cast<std::size_t>(
                                received
                            )
                        );

                        if(
                            request.rfind(
                                "GET /metrics ",
                                0
                            ) == 0
                        ) {

                            const std::string body =
                                LbMetrics::render(
                                    backend_pool,
                                    connections.size()
                                );

                            current.backend_to_client =
                                "HTTP/1.1 200 OK\r\n"
                                "Content-Type: text/plain; "
                                "charset=utf-8\r\n"
                                "Content-Length: " +
                                std::to_string(body.size()) +
                                "\r\n"
                                "Connection: close\r\n"
                                "\r\n" +
                                body;

                            current.backend_to_client_offset =
                                0;

                            current.client_read_closed =
                                true;

                            const uint32_t client_events =
                                EPOLLOUT |
                                EPOLLERR |
                                EPOLLHUP |
                                EPOLLRDHUP;

                            if(
                                client_events !=
                                current.current_client_events
                            ) {
                                if(epoll.modify(
                                    current.client_fd,
                                    client_events
                                )) {
                                    current.current_client_events =
                                        client_events;
                                }
                            }

                        } else {

                            closeConnection(
                                current.client_fd
                            );
                        }

                        continue;
                    }

                    forwardData(
                        current.client_fd,
                        current.client_to_backend,
                        current.client_to_backend_offset
                    );
                }
            }

            if(
                (event_flags & EPOLLRDHUP) &&
                is_backend_fd
            ) {

                current_it =
                    connections.find(client_fd);

                if(
                    current_it ==
                    connections.end()
                ) {
                    continue;
                }

                ConnectionPair& current =
                    current_it->second;

                current.backend_read_closed =
                    true;

                shutdown(
                    current.client_fd,
                    SHUT_WR
                );
            }

            current_it =
                connections.find(client_fd);

            if(
                current_it ==
                connections.end()
            ) {
                continue;
            }

            ConnectionPair& updated =
                current_it->second;

            if(
                is_backend_fd &&
                updated.backend_connecting &&
                (event_flags & EPOLLOUT)
            ) {

                int socket_error = 0;

                socklen_t socket_error_size =
                    sizeof(socket_error);

                if(
                    getsockopt(
                        updated.backend_fd,
                        SOL_SOCKET,
                        SO_ERROR,
                        &socket_error,
                        &socket_error_size
                    ) == -1 ||
                    socket_error != 0
                ) {

                    const int failed_client_fd =
                        updated.client_fd;

                    const std::size_t failed_backend_index =
                        updated.backend_index;

                    failoverConnection(
                        failed_client_fd,
                        failed_backend_index
                    );

                    continue;
                }

                updated.backend_connecting =
                    false;
            }

            current_it =
                connections.find(client_fd);

            if(
                current_it ==
                connections.end()
            ) {
                continue;
            }

            ConnectionPair& ready =
                current_it->second;

            if(
                ready.backend_fd != -1 &&
                !ready.backend_connecting &&
                ready.client_to_backend_offset <
                ready.client_to_backend.size()
            ) {

                flushData(
                    ready.backend_fd,
                    ready.client_to_backend,
                    ready.client_to_backend_offset
                );
            }

            current_it =
                connections.find(client_fd);

            if(
                current_it ==
                connections.end()
            ) {
                continue;
            }

            ConnectionPair& response =
                current_it->second;

            if(
                response.backend_to_client_offset <
                response.backend_to_client.size()
            ) {

                flushData(
                    response.client_fd,
                    response.backend_to_client,
                    response.backend_to_client_offset
                );
            }

            current_it =
                connections.find(client_fd);

            if(
                current_it ==
                connections.end()
            ) {
                continue;
            }

            ConnectionPair& after_io =
                current_it->second;

            if(
                after_io.client_to_backend_offset ==
                after_io.client_to_backend.size()
            ) {

                after_io.client_to_backend.clear();

                after_io.client_to_backend_offset = 0;
            }

            if(
                after_io.backend_to_client_offset ==
                after_io.backend_to_client.size()
            ) {

                after_io.backend_to_client.clear();

                after_io.backend_to_client_offset = 0;
            }

            if(
                after_io.metrics_connection &&
                after_io.backend_to_client.empty()
            ) {

                closeConnection(
                    after_io.client_fd
                );

                continue;
            }

            if(
                after_io.client_read_closed &&
                after_io.backend_read_closed &&
                after_io.client_to_backend.empty() &&
                after_io.backend_to_client.empty()
            ) {

                closeConnection(
                    after_io.client_fd
                );

                continue;
            }

            if(after_io.backend_fd != -1) {

                uint32_t backend_events =
                    EPOLLRDHUP |
                    EPOLLERR |
                    EPOLLHUP;

                if(!after_io.backend_read_closed) {
                    backend_events |= EPOLLIN;
                }

                if(
                    after_io.backend_connecting ||
                    after_io.client_to_backend_offset <
                    after_io.client_to_backend.size()
                ) {
                    backend_events |= EPOLLOUT;
                }

                if(
                    backend_events !=
                    after_io.current_backend_events
                ) {
                    if(epoll.modify(
                        after_io.backend_fd,
                        backend_events
                    )) {
                        after_io.current_backend_events =
                            backend_events;
                    }
                }
            }

            uint32_t client_events =
                EPOLLRDHUP |
                EPOLLERR |
                EPOLLHUP;

            if(!after_io.client_read_closed) {
                client_events |= EPOLLIN;
            }

            if(
                after_io.backend_to_client_offset <
                after_io.backend_to_client.size()
            ) {
                client_events |= EPOLLOUT;
            }

            if(
                client_events !=
                after_io.current_client_events
            ) {
                if(epoll.modify(
                    after_io.client_fd,
                    client_events
                )) {
                    after_io.current_client_events =
                        client_events;
                }
            }
        }
    }

    closeAllConnections();

    health_checker.cancelAll(epoll);

    std::cout
        << "LB Reactor "
        << reactor_id
        << " stopped"
        << std::endl;
}

void LoadBalancer::handleAccept() {
    while(true) {

        sockaddr_in client_address{};

        socklen_t client_address_length =
            sizeof(client_address);

        int client_fd = accept4(
            listen_socket.getFd(),
            reinterpret_cast<sockaddr*>(&client_address),
            &client_address_length,
            SOCK_NONBLOCK |
            SOCK_CLOEXEC
        );

        if(client_fd == -1) {

            if(
                errno == EAGAIN ||
                errno == EWOULDBLOCK
            ) {
                return;
            }

            if(errno == EINTR) {
                continue;
            }

            std::cerr
                << "LB Reactor "
                << reactor_id
                << " accept4() failed: "
                << std::strerror(errno)
                << std::endl;

            return;
        }

        Socket::setNoDelay(client_fd);

        if(connections.size() >= MAX_CONNECTIONS) {
            close(client_fd);
            continue;
        }

        ConnectionPair connection;

        connection.client_fd = client_fd;

        uint32_t client_events =
            EPOLLIN |
            EPOLLRDHUP |
            EPOLLERR |
            EPOLLHUP;

        connection.current_client_events =
            client_events;

        char client_ip[INET_ADDRSTRLEN]{};

        if(
            inet_ntop(
                AF_INET,
                &client_address.sin_addr,
                client_ip,
                sizeof(client_ip)
            ) != nullptr
        ) {
            connection.client_ip = client_ip;
        }

        connections.emplace(
            client_fd,
            std::move(connection)
        );

        if(!epoll.add(
            client_fd,
            client_events
        )) {

            connections.erase(client_fd);
            close(client_fd);
            continue;
        }
    }
}

std::size_t LoadBalancer::selectRoundRobinBackend() {
    const std::size_t count =
        backend_pool.size();

    if(count == 0) {
        return 0;
    }

    for(std::size_t attempt = 0; attempt < count; ++attempt) {

        const std::size_t index =
            next_backend_index % count;

        next_backend_index =
            (next_backend_index + 1) % count;

        const Backend& backend =
            backend_pool.getBackend(index);

        if(backend.healthy) {
            return index;
        }
    }

    return count;
}

std::optional<std::size_t> LoadBalancer::selectBackend(
    const std::string& client_ip
) {
    if(!backend_pool.hasHealthyBackend()) {
        return std::nullopt;
    }

    if(routing_mode == RoutingMode::RoundRobin) {

        const std::size_t index =
            selectRoundRobinBackend();

        if(index >= backend_pool.size()) {
            return std::nullopt;
        }

        return index;
    }

    return consistent_hash.getBackend(
        client_ip,
        backend_pool
    );
}

bool LoadBalancer::connectClientToBackend(
    int client_fd
) {
    if(!backend_pool.hasHealthyBackend()) {
        return false;
    }

    auto connection_it =
        connections.find(client_fd);

    if(
        connection_it ==
        connections.end()
    ) {
        return false;
    }

    ConnectionPair& connection =
        connection_it->second;

    std::optional<std::size_t> initial_backend =
        selectBackend(connection.client_ip);

    if(!initial_backend.has_value()) {
        return false;
    }

    const std::size_t backend_count =
        backend_pool.size();

    std::vector<bool> attempted(
        backend_count,
        false
    );

    for(std::size_t attempt = 0; attempt < backend_count; ++attempt) {

        std::size_t index;

        if(
            attempt == 0 ||
            routing_mode == RoutingMode::RoundRobin
        ) {
            if(attempt == 0) {
                index =
                    initial_backend.value();
            } else {

                const std::size_t next =
                    selectRoundRobinBackend();

                if(next >= backend_count) {
                    break;
                }

                index = next;
            }

        } else {

            index = initial_backend.value();

            bool found_alternate = false;

            for(std::size_t candidate = 0;
                candidate < backend_count;
                ++candidate) {

                if(
                    !attempted[candidate] &&
                    backend_pool.getBackend(candidate).healthy
                ) {
                    index = candidate;
                    found_alternate = true;
                    break;
                }
            }

            if(!found_alternate) {
                break;
            }
        }

        if(index >= backend_count) {
            continue;
        }

        if(attempted[index]) {
            continue;
        }

        attempted[index] = true;

        Backend& backend =
            backend_pool.getBackend(index);

        if(!backend.healthy) {
            continue;
        }

        bool connecting = false;

        int backend_fd =
            connectToBackend(
                backend,
                connecting
            );

        if(backend_fd == -1) {
            backend.failed_connections++;
            continue;
        }

        connection.backend_fd =
            backend_fd;

        connection.backend_index =
            index;

        connection.backend_connecting =
            connecting;

        backend.connection_count++;
        backend.total_connections++;

        uint32_t backend_events =
            EPOLLRDHUP |
            EPOLLERR |
            EPOLLHUP;

        if(connecting) {
            backend_events |= EPOLLOUT;
        } else {
            backend_events |= EPOLLIN;
        }

        if(!epoll.add(
            backend_fd,
            backend_events
        )) {

            if(backend.connection_count > 0) {
                backend.connection_count--;
            }

            close(backend_fd);

            connection.backend_fd = -1;

            continue;
        }

        connection.current_backend_events =
            backend_events;

        backend_to_client[backend_fd] =
            client_fd;

        return true;
    }

    return false;
}

int LoadBalancer::connectToBackend(
    const Backend& backend,
    bool& connecting
) {
    connecting = false;

    addrinfo hints{};

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;

    const std::string port_string =
        std::to_string(backend.port);

    const int resolve_result =
        getaddrinfo(
            backend.host.c_str(),
            port_string.c_str(),
            &hints,
            &result
        );

    if(
        resolve_result != 0 ||
        result == nullptr
    ) {
        return -1;
    }

    int fd = -1;

    for(
        addrinfo* address = result;
        address != nullptr;
        address = address->ai_next
    ) {

        fd = socket(
            address->ai_family,
            address->ai_socktype |
            SOCK_NONBLOCK |
            SOCK_CLOEXEC,
            address->ai_protocol
        );

        if(fd == -1) {
            continue;
        }

        if(!Socket::setNoDelay(fd)) {
            close(fd);
            fd = -1;
            continue;
        }

        const int connect_result =
            connect(
                fd,
                address->ai_addr,
                address->ai_addrlen
            );

        if(connect_result == 0) {

            freeaddrinfo(result);

            return fd;
        }

        if(errno == EINPROGRESS) {

            connecting = true;

            freeaddrinfo(result);

            return fd;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(result);

    return -1;
}

void LoadBalancer::forwardData(
    int source_fd,
    std::string& output_buffer,
    std::size_t& offset
) {
    char buffer[64 * 1024];

    const std::size_t pending_bytes =
        output_buffer.size() - offset;

    const ssize_t bytes_read =
        recv(
            source_fd,
            buffer,
            sizeof(buffer),
            0
        );

    if(bytes_read > 0) {

        if(
            pending_bytes +
            static_cast<std::size_t>(bytes_read)
            > MAX_BUFFER_SIZE
        ) {

            auto backend_it =
                backend_to_client.find(source_fd);

            if(
                backend_it !=
                backend_to_client.end()
            ) {

                closeConnection(
                    backend_it->second
                );

            } else {

                closeConnection(source_fd);
            }

            return;
        }

        if(offset == output_buffer.size()) {

            output_buffer.clear();
            offset = 0;
        }

        output_buffer.append(
            buffer,
            static_cast<std::size_t>(bytes_read)
        );

        return;
    }

    if(bytes_read == 0) {

        auto backend_it =
            backend_to_client.find(source_fd);

        if(
            backend_it !=
            backend_to_client.end()
        ) {

            auto connection_it =
                connections.find(
                    backend_it->second
                );

            if(
                connection_it !=
                connections.end()
            ) {

                connection_it->second.backend_read_closed =
                    true;
            }

        } else {

            auto connection_it =
                connections.find(source_fd);

            if(
                connection_it !=
                connections.end()
            ) {

                connection_it->second.client_read_closed =
                    true;
            }
        }

        return;
    }

    if(errno == EINTR) {
        return;
    }

    if(
        errno == EAGAIN ||
        errno == EWOULDBLOCK
    ) {
        return;
    }

    auto backend_it =
        backend_to_client.find(source_fd);

    if(
        backend_it !=
        backend_to_client.end()
    ) {

        auto connection_it =
            connections.find(
                backend_it->second
            );

        if(
            connection_it !=
            connections.end()
        ) {

            closeConnection(
                connection_it->second.client_fd
            );
        }

    } else {

        closeConnection(source_fd);
    }

    return;
}

void LoadBalancer::flushData(
    int destination_fd,
    std::string& output_buffer,
    std::size_t& offset
) {
    while(offset < output_buffer.size()) {

        const ssize_t bytes_sent =
            send(
                destination_fd,
                output_buffer.data() + offset,
                output_buffer.size() - offset,
                MSG_NOSIGNAL
            );

        if(bytes_sent > 0) {

            offset +=
                static_cast<std::size_t>(
                    bytes_sent
                );

            continue;
        }

        if(
            bytes_sent == -1 &&
            (
                errno == EAGAIN ||
                errno == EWOULDBLOCK
            )
        ) {
            return;
        }

        if(
            bytes_sent == -1 &&
            errno == EINTR
        ) {
            continue;
        }

        auto backend_it =
            backend_to_client.find(destination_fd);

        if(
            backend_it !=
            backend_to_client.end()
        ) {

            closeConnection(
                backend_it->second
            );

        } else {

            closeConnection(destination_fd);
        }

        return;
    }
}

void LoadBalancer::updateWriteInterest(
    int fd,
    bool enabled
) {
    auto backend_it =
        backend_to_client.find(fd);

    if(
        backend_it !=
        backend_to_client.end()
    ) {

        auto connection_it =
            connections.find(
                backend_it->second
            );

        if(
            connection_it ==
            connections.end()
        ) {
            return;
        }

        ConnectionPair& connection =
            connection_it->second;

        uint32_t events =
            EPOLLRDHUP |
            EPOLLERR |
            EPOLLHUP;

        if(connection.backend_connecting) {
            events |= EPOLLOUT;
        } else if(
            !connection.backend_read_closed
        ) {
            events |= EPOLLIN;
        }

        if(enabled) {
            events |= EPOLLOUT;
        }

        if(
            events !=
            connection.current_backend_events
        ) {
            if(epoll.modify(
                fd,
                events
            )) {
                connection.current_backend_events =
                    events;
            }
        }

        return;
    }

    auto connection_it =
        connections.find(fd);

    if(
        connection_it ==
        connections.end()
    ) {
        return;
    }

    ConnectionPair& connection =
        connection_it->second;

    uint32_t events =
        EPOLLRDHUP |
        EPOLLERR |
        EPOLLHUP;

    if(!connection.client_read_closed) {
        events |= EPOLLIN;
    }

    if(enabled) {
        events |= EPOLLOUT;
    }

    if(
        events !=
        connection.current_client_events
    ) {
        if(epoll.modify(
            fd,
            events
        )) {
            connection.current_client_events =
                events;
        }
    }
}

void LoadBalancer::handleHealthCheck() {
    health_timer.consume();

    health_checker.start(
        backend_pool,
        epoll
    );
}

void LoadBalancer::handleHealthEvent(
    int fd,
    uint32_t events
) {
    health_checker.handleEvent(
        fd,
        events,
        epoll
    );
}

void LoadBalancer::closeConnection(
    int client_fd
) {
    auto connection_it =
        connections.find(client_fd);

    if(
        connection_it ==
        connections.end()
    ) {
        return;
    }

    ConnectionPair& connection =
        connection_it->second;

    if(connection.closing) {
        return;
    }

    connection.closing = true;

    epoll.remove(
        connection.client_fd
    );

    if(connection.backend_fd != -1) {

        epoll.remove(
            connection.backend_fd
        );

        backend_to_client.erase(
            connection.backend_fd
        );

        if(
            connection.backend_index <
            backend_pool.size()
        ) {

            Backend& backend =
                backend_pool.getBackend(
                    connection.backend_index
                );

            if(backend.connection_count > 0) {
                backend.connection_count--;
            }
        }

        close(
            connection.backend_fd
        );

        connection.backend_fd = -1;
    }

    close(
        connection.client_fd
    );

    connections.erase(
        connection_it
    );
}

bool LoadBalancer::failoverConnection(
    int client_fd,
    std::size_t failed_backend_index
) {
    auto connection_it =
        connections.find(client_fd);

    if(
        connection_it ==
        connections.end()
    ) {
        return false;
    }

    ConnectionPair& connection =
        connection_it->second;

    const int old_backend_fd =
        connection.backend_fd;

    if(old_backend_fd != -1) {

        epoll.remove(old_backend_fd);

        backend_to_client.erase(old_backend_fd);

        if(
            failed_backend_index <
            backend_pool.size()
        ) {

            Backend& failed_backend =
                backend_pool.getBackend(
                    failed_backend_index
                );

            if(failed_backend.connection_count > 0) {
                failed_backend.connection_count--;
            }

            failed_backend.failed_connections++;
        }

        close(old_backend_fd);

        connection.backend_fd = -1;
        connection.backend_connecting = false;
        connection.current_backend_events = 0;
    }

    const std::size_t backend_count =
        backend_pool.size();

    if(backend_count == 0) {
        closeConnection(client_fd);
        return false;
    }

    const std::size_t start_index =
        (failed_backend_index + 1) % backend_count;

    for(
        std::size_t attempt = 0;
        attempt < backend_count;
        ++attempt
    ) {

        const std::size_t index =
            (start_index + attempt) % backend_count;

        if(index == failed_backend_index) {
            continue;
        }

        Backend& backend =
            backend_pool.getBackend(index);

        if(!backend.healthy) {
            continue;
        }

        bool connecting = false;

        const int backend_fd =
            connectToBackend(
                backend,
                connecting
            );

        if(backend_fd == -1) {
            backend.failed_connections++;
            continue;
        }

        connection.backend_fd =
            backend_fd;

        connection.backend_index =
            index;

        connection.backend_connecting =
            connecting;

        backend.connection_count++;
        backend.total_connections++;

        uint32_t backend_events =
            EPOLLRDHUP |
            EPOLLERR |
            EPOLLHUP;

        if(connecting) {
            backend_events |= EPOLLOUT;
        } else {
            backend_events |= EPOLLIN;
        }

        if(!epoll.add(
            backend_fd,
            backend_events
        )) {

            if(backend.connection_count > 0) {
                backend.connection_count--;
            }

            close(backend_fd);

            connection.backend_fd = -1;
            connection.backend_connecting = false;
            connection.current_backend_events = 0;

            continue;
        }

        connection.current_backend_events =
            backend_events;

        backend_to_client[backend_fd] =
            client_fd;

        return true;
    }

    closeConnection(client_fd);
    return false;
}

void LoadBalancer::closeAllConnections() {
    for(
        auto& [client_fd, connection] :
        connections
    ) {

        epoll.remove(client_fd);

        if(connection.backend_fd != -1) {

            epoll.remove(
                connection.backend_fd
            );

            if(
                connection.backend_index <
                backend_pool.size()
            ) {

                Backend& backend =
                    backend_pool.getBackend(
                        connection.backend_index
                    );

                if(backend.connection_count > 0) {
                    backend.connection_count--;
                }
            }

            close(
                connection.backend_fd
            );
        }

        close(client_fd);
    }

    connections.clear();

    backend_to_client.clear();
}