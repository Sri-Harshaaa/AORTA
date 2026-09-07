#pragma once

#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"

#include <cstddef>
#include <string>

class StaticFileHandler {

private:
    static constexpr std::size_t MAX_FILE_SIZE = 16 * 1024 * 1024;

    std::string public_directory;

    /*
     * The document root resolved through realpath() at construction. Every
     * served path must resolve inside it, which a substring check for ".."
     * cannot guarantee on its own - a symlink inside the root would still
     * escape.
     */
    std::string canonical_root;

    std::string getFilePath(const std::string& target) const;
    std::string getContentType(const std::string& path) const;
    bool isSafePath(const std::string& target) const;

    bool resolveWithinRoot(
        const std::string& path,
        std::string& resolved
    ) const;

public:
    explicit StaticFileHandler(const std::string& directory);

    bool handle(const HttpRequest& request, HttpResponse& response);
};