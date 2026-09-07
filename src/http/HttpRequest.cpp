#include "http/HttpRequest.hpp"

#include <cctype>

static bool equalsIgnoreCase(const std::string& left, const std::string& right) {
    if(left.size() != right.size()) {
        return false;
    }

    for(std::size_t i = 0; i < left.size(); i++) {
        if(std::tolower(static_cast<unsigned char>(left[i])) != std::tolower(static_cast<unsigned char>(right[i]))) {
            return false;
        }
    }

    return true;
}


static std::string trim(const std::string& value) {
    std::size_t start = 0;
    std::size_t end = value.size();

    while(start < end && (value[start] == ' ' || value[start] == '\t')) {
        start++;
    }

    while(end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) {
        end--;
    }

    return value.substr(
        start,
        end - start
    );
}


std::string HttpRequest::getHeader(const std::string& name) const {
    for(const auto& header : headers) {
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
            return header.second;
        }
    }

    return "";
}


bool HttpRequest::hasHeaderToken(const std::string& name, const std::string& token) const {
    for(const auto& header : headers) {
        if(!equalsIgnoreCase(header.first, name)) {
            continue;
        }

        std::size_t start = 0;

        while(start < header.second.size()) {
            std::size_t comma = header.second.find(',', start);

            std::size_t end = comma;

            if(end == std::string::npos) {
                end = header.second.size();
            }

            std::string current_token = trim(
                header.second.substr(
                    start,
                    end - start
                )
            );

            if(equalsIgnoreCase(current_token, token)) {
                return true;
            }

            if(comma == std::string::npos) {
                break;
            }

            start = comma + 1;
        }
    }

    return false;
}