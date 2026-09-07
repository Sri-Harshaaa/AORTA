#pragma once

#include "http/HttpParser.hpp"

#include <cstddef>
#include <deque>
#include <string>

class Connection {

private:
    static constexpr std::size_t MAX_RECEIVE_BUFFER = 64 * 1024;
    static constexpr std::size_t MAX_OUTPUT_QUEUE_SIZE = 8 * 1024 * 1024;

    struct OutputBuffer {
        enum class Type {
            Memory,
            File
        };

        Type type{Type::Memory};

        std::string data;
        std::size_t offset{0};

        int file_fd{-1};
        std::size_t file_size{0};
    };

    int fd{-1};
    int reactor_id{-1};

    std::string receive_buffer;
    std::deque<OutputBuffer> output_queue;

    std::size_t output_queue_size{0};

    HttpParser parser;

    bool close_after_write{false};

    void closeOutputFile(OutputBuffer& output);

public:
    enum class ReadResult {
        DataReceived,
        Disconnected,
        Error,
        BufferFull,
        WouldBlock
    };

    enum class WriteResult {
        Complete,
        WouldBlock,
        Error
    };

    explicit Connection(int fd, int reactor_id);

    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    int getFd() const;
    int getReactorId() const;

    ReadResult read();
    WriteResult write();

    const std::string& getReceiveBuffer() const;

    std::size_t consumeParsedBytes();

    HttpParser::ParseResult parseRequest();

    const HttpRequest& getRequest() const;

    std::size_t getConsumedBytes() const;

    void resetParser();

    bool queueResponse(const std::string& response);

    bool queueFile(
        const std::string& path,
        std::size_t file_size
    );

    bool hasPendingOutput() const;

    std::size_t getOutputQueueSize() const;

    void setCloseAfterWrite(bool value);
    bool shouldCloseAfterWrite() const;
};