#pragma once

#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"

#include <cstddef>
#include <string>

class StaticFileHandler {

private:
    static constexpr std::size_t MAX_FILE_SIZE = 16 * 1024 * 1024;

    std::string public_directory;

    std::string getFilePath(const std::string& target) const;
    std::string getContentType(const std::string& path) const;
    bool isSafePath(const std::string& target) const;

public:
    explicit StaticFileHandler(const std::string& directory);

    bool handle(const HttpRequest& request, HttpResponse& response);
};