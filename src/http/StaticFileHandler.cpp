#include "http/StaticFileHandler.hpp"

#include <cstdlib>

#include <stdlib.h>
#include <sys/stat.h>

StaticFileHandler::StaticFileHandler(const std::string& directory)
    : public_directory(directory) {

    char* resolved = realpath(directory.c_str(), nullptr);

    if(resolved != nullptr) {
        canonical_root = resolved;
        free(resolved);
    }
}


bool StaticFileHandler::resolveWithinRoot(
    const std::string& path,
    std::string& resolved_path
) const {
    if(canonical_root.empty()) {
        return false;
    }

    char* resolved = realpath(path.c_str(), nullptr);

    if(resolved == nullptr) {
        return false;
    }

    resolved_path = resolved;

    free(resolved);

    if(resolved_path == canonical_root) {
        return true;
    }

    /*
     * The separator matters: without it "/srv/public-secrets" would pass a
     * prefix test against a root of "/srv/public".
     */
    if(resolved_path.size() <= canonical_root.size()) {
        return false;
    }

    if(resolved_path.compare(0, canonical_root.size(), canonical_root) != 0) {
        return false;
    }

    return resolved_path[canonical_root.size()] == '/';
}

bool StaticFileHandler::isSafePath(const std::string& target) const {
    if(target.empty()) {
        return false;
    }

    if(target.find("..") != std::string::npos) {
        return false;
    }

    if(target.find('\\') != std::string::npos) {
        return false;
    }

    return true;
}

std::string StaticFileHandler::getFilePath(const std::string& target) const {
    if(target == "/") {
        return public_directory + "/index.html";
    }

    return public_directory + target;
}

std::string StaticFileHandler::getContentType(const std::string& path) const {
    if(path.ends_with(".html")) {
        return "text/html";
    }

    if(path.ends_with(".css")) {
        return "text/css";
    }

    if(path.ends_with(".js")) {
        return "application/javascript";
    }

    if(path.ends_with(".json")) {
        return "application/json";
    }

    if(path.ends_with(".png")) {
        return "image/png";
    }

    if(path.ends_with(".jpg") || path.ends_with(".jpeg")) {
        return "image/jpeg";
    }

    if(path.ends_with(".svg")) {
        return "image/svg+xml";
    }

    if(path.ends_with(".txt")) {
        return "text/plain";
    }

    return "application/octet-stream";
}

bool StaticFileHandler::handle(const HttpRequest& request, HttpResponse& response) {
    if(request.method != "GET" && request.method != "HEAD") {
        return false;
    }

    if(!isSafePath(request.target)) {
        response.setStatus(403, "Forbidden");
        response.setBody("Forbidden");
        response.setContentType("text/plain");
        response.setConnection("keep-alive");
        response.setContentLength();
        return true;
    }

    std::string file_path;

    if(!resolveWithinRoot(getFilePath(request.target), file_path)) {
        response.setStatus(404, "Not Found");
        response.setBody("Not Found");
        response.setContentType("text/plain");
        response.setConnection("keep-alive");
        response.setContentLength();
        return true;
    }

    struct stat file_info;

    if(stat(file_path.c_str(), &file_info) == -1) {
        response.setStatus(404, "Not Found");
        response.setBody("Not Found");
        response.setContentType("text/plain");
        response.setConnection("keep-alive");
        response.setContentLength();
        return true;
    }

    if(!S_ISREG(file_info.st_mode)) {
        response.setStatus(404, "Not Found");
        response.setBody("Not Found");
        response.setContentType("text/plain");
        response.setConnection("keep-alive");
        response.setContentLength();
        return true;
    }

    std::size_t file_size = static_cast<std::size_t>(file_info.st_size);

    if(file_size > MAX_FILE_SIZE) {
        response.setStatus(413, "Content Too Large");
        response.setBody("Content Too Large");
        response.setContentType("text/plain");
        response.setConnection("keep-alive");
        response.setContentLength();
        return true;
    }

    response.setStatus(200, "OK");
    response.setFile(
        file_path,
        file_size
    );
    response.setContentType(getContentType(file_path));
    response.setConnection("keep-alive");
    response.setContentLength();

    if(request.method == "HEAD") {
        response.setSendBody(false);
    }

    return true;
}