#pragma once

#include "http/HttpRequest.hpp"

#include <cstddef>
#include <string>

class HttpParser {

public:
    enum class State {
        RequestLine,
        Headers,
        Body,
        ChunkSize,
        ChunkData,
        ChunkDataCRLF,
        ChunkTrailers
    };

    enum class ParseResult {
        NeedMoreData,
        Complete,
        BadRequest,
        RequestLineTooLarge,
        HeadersTooLarge,
        TooManyHeaders,
        BodyTooLarge,
        UnsupportedTransferEncoding,
        UnsupportedVersion
    };

private:
    static constexpr std::size_t MAX_REQUEST_LINE_SIZE = 8 * 1024;
    static constexpr std::size_t MAX_HEADER_LINE_SIZE = 8 * 1024;
    static constexpr std::size_t MAX_HEADERS_SIZE = 32 * 1024;
    static constexpr std::size_t MAX_HEADER_COUNT = 100;
    static constexpr std::size_t MAX_BODY_SIZE = 1024 * 1024;

    State state{State::RequestLine};

    HttpRequest request;

    std::size_t cursor{0};
    std::size_t content_length{0};
    std::size_t headers_size{0};
    std::size_t header_count{0};
    std::size_t host_count{0};

    std::size_t chunk_size{0};
    std::size_t chunk_bytes_read{0};
    std::size_t trailer_size{0};
    std::size_t trailer_count{0};

    bool has_content_length{false};
    bool has_transfer_encoding{false};
    bool chunked_transfer_encoding{false};

    static bool equalsIgnoreCase(const std::string& left, const std::string& right);
    static bool isValidHeaderName(const std::string& name);
    static bool isValidMethod(const std::string& method);
    static bool isValidTarget(const std::string& target);
    static bool isValidChunkSize(const std::string& value, std::size_t& size);
    static std::string trim(const std::string& value);

public:
    ParseResult parse(const std::string& buffer);

    const HttpRequest& getRequest() const;

    State getState() const;

    std::size_t getConsumedBytes() const;

    std::size_t consumeParsedBytes();

    void reset();
};