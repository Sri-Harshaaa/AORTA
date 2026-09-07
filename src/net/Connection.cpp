#include "net/Connection.hpp"

#include <cerrno>
#include <fcntl.h>
#include <utility>

#include <sys/sendfile.h>
#include <sys/socket.h>
#include <unistd.h>

Connection::Connection(int fd, int reactor_id) : fd(fd), reactor_id(reactor_id) {
}


Connection::~Connection() {
    for(auto& output : output_queue) {
        closeOutputFile(output);
    }

    if(fd != -1) {
        close(fd);
    }
}


int Connection::getFd() const {
    return fd;
}


int Connection::getReactorId() const {
    return reactor_id;
}


Connection::ReadResult Connection::read() {
    if(receive_buffer.size() >= MAX_RECEIVE_BUFFER) {
        return ReadResult::BufferFull;
    }

    char buffer[4096];

    std::size_t remaining = MAX_RECEIVE_BUFFER - receive_buffer.size();

    std::size_t bytes_to_read = remaining;

    if(bytes_to_read > sizeof(buffer)) {
        bytes_to_read = sizeof(buffer);
    }

    ssize_t bytes_received = recv(
        fd,
        buffer,
        bytes_to_read,
        0
    );

    if(bytes_received > 0) {
        receive_buffer.append(buffer, bytes_received);
        return ReadResult::DataReceived;
    }

    if(bytes_received == 0) {
        return ReadResult::Disconnected;
    }

    if(errno == EINTR) {
        return read();
    }

    if(errno == EAGAIN || errno == EWOULDBLOCK) {
        return ReadResult::WouldBlock;
    }

    return ReadResult::Error;
}


Connection::WriteResult Connection::write() {
    while(!output_queue.empty()) {
        OutputBuffer& output = output_queue.front();

        if(output.type == OutputBuffer::Type::Memory) {
            if(output.offset >= output.data.size()) {
                output_queue_size -= output.data.size();
                output_queue.pop_front();
                continue;
            }

            ssize_t bytes_sent = send(
                fd,
                output.data.data() + output.offset,
                output.data.size() - output.offset,
                MSG_NOSIGNAL
            );

            if(bytes_sent > 0) {
                output.offset += bytes_sent;

                if(output.offset == output.data.size()) {
                    output_queue_size -= output.data.size();
                    output_queue.pop_front();
                }

                continue;
            }

            if(bytes_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                return WriteResult::WouldBlock;
            }

            if(bytes_sent == -1 && errno == EINTR) {
                continue;
            }

            return WriteResult::Error;
        }

        if(output.type == OutputBuffer::Type::File) {
            if(output.offset >= output.file_size) {
                closeOutputFile(output);
                output_queue_size -= output.file_size;
                output_queue.pop_front();
                continue;
            }

            off_t file_offset = static_cast<off_t>(output.offset);

            ssize_t bytes_sent = sendfile(
                fd,
                output.file_fd,
                &file_offset,
                output.file_size - output.offset
            );

            if(bytes_sent > 0) {
                output.offset += static_cast<std::size_t>(bytes_sent);

                if(output.offset == output.file_size) {
                    closeOutputFile(output);
                    output_queue_size -= output.file_size;
                    output_queue.pop_front();
                }

                continue;
            }

            if(bytes_sent == 0) {
                closeOutputFile(output);
                output_queue_size -= output.file_size;
                output_queue.pop_front();
                continue;
            }

            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                return WriteResult::WouldBlock;
            }

            if(errno == EINTR) {
                continue;
            }

            return WriteResult::Error;
        }
    }

    return WriteResult::Complete;
}


const std::string& Connection::getReceiveBuffer() const {
    return receive_buffer;
}


std::size_t Connection::consumeParsedBytes() {
    std::size_t consumed = parser.consumeParsedBytes();

    if(consumed == 0) {
        return 0;
    }

    if(consumed >= receive_buffer.size()) {
        receive_buffer.clear();
        return consumed;
    }

    receive_buffer.erase(
        0,
        consumed
    );

    return consumed;
}


HttpParser::ParseResult Connection::parseRequest() {
    return parser.parse(receive_buffer);
}


const HttpRequest& Connection::getRequest() const {
    return parser.getRequest();
}


std::size_t Connection::getConsumedBytes() const {
    return parser.getConsumedBytes();
}


void Connection::resetParser() {
    parser.reset();
}


bool Connection::queueResponse(const std::string& response) {
    if(response.size() > MAX_OUTPUT_QUEUE_SIZE) {
        return false;
    }

    if(output_queue_size + response.size() > MAX_OUTPUT_QUEUE_SIZE) {
        return false;
    }

    OutputBuffer output;

    output.type = OutputBuffer::Type::Memory;
    output.data = response;
    output.offset = 0;

    output_queue.push_back(
        std::move(output)
    );

    output_queue_size += response.size();

    return true;
}


bool Connection::queueFile(
    const std::string& path,
    std::size_t file_size
) {
    if(file_size > MAX_OUTPUT_QUEUE_SIZE) {
        return false;
    }

    if(output_queue_size + file_size > MAX_OUTPUT_QUEUE_SIZE) {
        return false;
    }

    int file_fd = open(
        path.c_str(),
        O_RDONLY | O_CLOEXEC
    );

    if(file_fd == -1) {
        return false;
    }

    OutputBuffer output;

    output.type = OutputBuffer::Type::File;
    output.offset = 0;
    output.file_fd = file_fd;
    output.file_size = file_size;

    output_queue.push_back(
        std::move(output)
    );

    output_queue_size += file_size;

    return true;
}


bool Connection::hasPendingOutput() const {
    return !output_queue.empty();
}


std::size_t Connection::getOutputQueueSize() const {
    return output_queue_size;
}


void Connection::setCloseAfterWrite(bool value) {
    close_after_write = value;
}


bool Connection::shouldCloseAfterWrite() const {
    return close_after_write;
}


void Connection::closeOutputFile(OutputBuffer& output) {
    if(output.file_fd != -1) {
        close(output.file_fd);
        output.file_fd = -1;
    }
}