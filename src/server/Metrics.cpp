#include "server/Metrics.hpp"

#include <string>


void Metrics::incrementRequests() {
    requests_total++;
}


void Metrics::incrementResponses() {
    responses_total++;
}


void Metrics::incrementActiveConnections() {
    active_connections++;
}


void Metrics::decrementActiveConnections() {
    if(active_connections > 0) {
        active_connections--;
    }
}


void Metrics::incrementErrors() {
    errors_total++;
}


void Metrics::addBytesReceived(std::size_t bytes) {
    bytes_received += bytes;
}


void Metrics::addBytesSent(std::size_t bytes) {
    bytes_sent += bytes;
}


std::size_t Metrics::getRequests() const {
    return requests_total;
}


std::size_t Metrics::getResponses() const {
    return responses_total;
}


std::size_t Metrics::getActiveConnections() const {
    return active_connections;
}


std::size_t Metrics::getErrors() const {
    return errors_total;
}


std::size_t Metrics::getBytesReceived() const {
    return bytes_received;
}


std::size_t Metrics::getBytesSent() const {
    return bytes_sent;
}


std::string Metrics::serialize() const {
    std::string output;

    output += "aorta_requests_total ";
    output += std::to_string(requests_total);
    output += "\n";

    output += "aorta_responses_total ";
    output += std::to_string(responses_total);
    output += "\n";

    output += "aorta_active_connections ";
    output += std::to_string(active_connections);
    output += "\n";

    output += "aorta_errors_total ";
    output += std::to_string(errors_total);
    output += "\n";

    output += "aorta_bytes_received_total ";
    output += std::to_string(bytes_received);
    output += "\n";

    output += "aorta_bytes_sent_total ";
    output += std::to_string(bytes_sent);
    output += "\n";

    return output;
}