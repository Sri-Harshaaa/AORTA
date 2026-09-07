#pragma once

#include <cstddef>
#include <string>

class Metrics {

private:
    std::size_t requests_total{0};
    std::size_t responses_total{0};
    std::size_t active_connections{0};
    std::size_t errors_total{0};
    std::size_t bytes_received{0};
    std::size_t bytes_sent{0};

public:
    void incrementRequests();
    void incrementResponses();
    void incrementActiveConnections();
    void decrementActiveConnections();
    void incrementErrors();

    void addBytesReceived(std::size_t bytes);
    void addBytesSent(std::size_t bytes);

    std::size_t getRequests() const;
    std::size_t getResponses() const;
    std::size_t getActiveConnections() const;
    std::size_t getErrors() const;
    std::size_t getBytesReceived() const;
    std::size_t getBytesSent() const;

    std::string serialize() const;
};