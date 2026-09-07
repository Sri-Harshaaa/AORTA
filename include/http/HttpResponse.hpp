#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

class HttpResponse {

private:
    std::string version{"HTTP/1.1"};

    int status_code{200};
    std::string reason{"OK"};

    std::vector<std::pair<std::string, std::string>> headers;

    std::string body;

    bool send_body{true};

    bool send_file{false};
    std::string file_path;
    std::size_t file_size{0};

public:
    HttpResponse();

    void setStatus(int code, const std::string& reason_phrase);

    void setBody(const std::string& response_body);

    void setFile(const std::string& path, std::size_t size);

    void addHeader(const std::string& name, const std::string& value);

    void setHeader(const std::string& name, const std::string& value);

    void setContentType(const std::string& content_type);

    void setContentLength();

    void setContentLength(std::size_t length);

    void setConnection(const std::string& connection);

    void setSendBody(bool value);

    bool hasFile() const;

    const std::string& getFilePath() const;

    std::size_t getFileSize() const;

    std::string serialize() const;
};