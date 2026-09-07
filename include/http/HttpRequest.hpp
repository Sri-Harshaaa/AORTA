#pragma once

#include <string>
#include <vector>
#include <utility>

struct HttpRequest {
    std::string method;
    std::string target;
    std::string version;

    std::vector<std::pair<std::string, std::string>> headers;

    std::string body;

    std::string getHeader(const std::string& name) const;
    bool hasHeaderToken(const std::string& name, const std::string& token) const;
};