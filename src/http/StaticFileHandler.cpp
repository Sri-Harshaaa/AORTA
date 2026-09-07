#include "http/StaticFileHandler.hpp"

#include <sys/stat.h>

StaticFileHandler::StaticFileHandler(const std::string& directory) : public_directory(directory) {
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

    std::string file_path = getFilePath(request.target);

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