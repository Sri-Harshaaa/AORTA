#include "http/HttpResponse.hpp"

#include <cctype>

HttpResponse::HttpResponse() {
    headers.reserve(4);
}


void HttpResponse::setStatus(int code, const std::string& reason_phrase) {
    status_code = code;
    reason = reason_phrase;
}


void HttpResponse::setBody(const std::string& response_body) {
    body = response_body;
    send_file = false;
    file_path.clear();
    file_size = 0;
}


void HttpResponse::setFile(const std::string& path, std::size_t size) {
    file_path = path;
    file_size = size;
    send_file = true;
    body.clear();
}


void HttpResponse::addHeader(const std::string& name, const std::string& value) {
    headers.push_back({name, value});
}


void HttpResponse::setHeader(const std::string& name, const std::string& value) {
    for(auto& header : headers) {
        if(header.first.size() != name.size()) {
            continue;
        }

        bool same_name = true;

        for(std::size_t i = 0; i < name.size(); i++) {
            if(std::tolower(static_cast<unsigned char>(header.first[i])) != std::tolower(static_cast<unsigned char>(name[i]))) {
                same_name = false;
                break;
            }
        }

        if(same_name) {
            header.second = value;
            return;
        }
    }

    headers.push_back({name, value});
}


void HttpResponse::setContentType(const std::string& content_type) {
    setHeader("Content-Type", content_type);
}


void HttpResponse::setContentLength() {
    if(send_file) {
        setContentLength(file_size);
        return;
    }

    setContentLength(body.size());
}


void HttpResponse::setContentLength(std::size_t length) {
    setHeader(
        "Content-Length",
        std::to_string(length)
    );
}


void HttpResponse::setConnection(const std::string& connection) {
    setHeader("Connection", connection);
}


void HttpResponse::setSendBody(bool value) {
    send_body = value;
}


bool HttpResponse::hasFile() const {
    return send_file && !file_path.empty();
}


const std::string& HttpResponse::getFilePath() const {
    return file_path;
}


std::size_t HttpResponse::getFileSize() const {
    return file_size;
}


std::string HttpResponse::serialize() const {
    std::size_t response_size =
        version.size() +
        1 +
        3 +
        1 +
        reason.size() +
        2;

    for(const auto& header : headers) {
        response_size +=
            header.first.size() +
            2 +
            header.second.size() +
            2;
    }

    response_size += 2;

    if(send_body && !send_file) {
        response_size += body.size();
    }

    std::string response;
    response.reserve(response_size);

    response += version;
    response += " ";
    response += std::to_string(status_code);
    response += " ";
    response += reason;
    response += "\r\n";

    for(const auto& header : headers) {
        response += header.first;
        response += ": ";
        response += header.second;
        response += "\r\n";
    }

    response += "\r\n";

    if(send_body && !send_file) {
        response += body;
    }

    return response;
}