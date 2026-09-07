#include "http/HttpParser.hpp"

#include <cctype>
#include <limits>
#include <string>

bool HttpParser::equalsIgnoreCase(const std::string& left, const std::string& right) {
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


bool HttpParser::isValidHeaderName(const std::string& name) {
    if(name.empty()) {
        return false;
    }

    for(char character : name) {
        unsigned char c = static_cast<unsigned char>(character);

        if(std::isalnum(c)) {
            continue;
        }

        if(character == '!' || character == '#' || character == '$' || character == '%' || character == '&' || character == '\'' || character == '*' || character == '+' || character == '-' || character == '.' || character == '^' || character == '_' || character == '`' || character == '|' || character == '~') {
            continue;
        }

        return false;
    }

    return true;
}


bool HttpParser::isValidMethod(const std::string& method) {
    if(method.empty()) {
        return false;
    }

    for(char character : method) {
        unsigned char c = static_cast<unsigned char>(character);

        if(std::isalnum(c)) {
            continue;
        }

        if(character == '!' || character == '#' || character == '$' || character == '%' || character == '&' || character == '\'' || character == '*' || character == '+' || character == '-' || character == '.' || character == '^' || character == '_' || character == '`' || character == '|' || character == '~') {
            continue;
        }

        return false;
    }

    return true;
}


bool HttpParser::isValidTarget(const std::string& target) {
    if(target.empty()) {
        return false;
    }

    for(unsigned char character : target) {
        if(character < 0x20 || character == 0x7F) {
            return false;
        }
    }

    return true;
}


std::string HttpParser::trim(const std::string& value) {
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


bool HttpParser::isValidChunkSize(const std::string& value, std::size_t& size) {
    std::string chunk_value = value;

    std::size_t extension = chunk_value.find(';');

    if(extension != std::string::npos) {
        chunk_value = chunk_value.substr(0, extension);
    }

    chunk_value = trim(chunk_value);

    if(chunk_value.empty()) {
        return false;
    }

    size = 0;

    for(char character : chunk_value) {
        unsigned char c = static_cast<unsigned char>(character);

        if(!std::isxdigit(c)) {
            return false;
        }

        std::size_t digit = 0;

        if(character >= '0' && character <= '9') {
            digit = character - '0';
        } else if(character >= 'a' && character <= 'f') {
            digit = character - 'a' + 10;
        } else {
            digit = character - 'A' + 10;
        }

        if(size > (MAX_BODY_SIZE - digit) / 16) {
            return false;
        }

        size = size * 16 + digit;
    }

    return true;
}


HttpParser::ParseResult HttpParser::parse(const std::string& buffer) {
    while(true) {
        if(state == State::RequestLine) {
            std::size_t end = buffer.find("\r\n", cursor);

            if(end == std::string::npos) {
                if(buffer.size() - cursor > MAX_REQUEST_LINE_SIZE) {
                    return ParseResult::RequestLineTooLarge;
                }

                return ParseResult::NeedMoreData;
            }

            if(end - cursor > MAX_REQUEST_LINE_SIZE) {
                return ParseResult::RequestLineTooLarge;
            }

            std::string request_line = buffer.substr(
                cursor,
                end - cursor
            );

            std::size_t first_space = request_line.find(' ');

            if(first_space == std::string::npos) {
                return ParseResult::BadRequest;
            }

            std::size_t second_space = request_line.find(' ', first_space + 1);

            if(second_space == std::string::npos) {
                return ParseResult::BadRequest;
            }

            if(request_line.find(' ', second_space + 1) != std::string::npos) {
                return ParseResult::BadRequest;
            }

            if(first_space == 0 || second_space == first_space + 1 || second_space + 1 >= request_line.size()) {
                return ParseResult::BadRequest;
            }

            request.method = request_line.substr(0, first_space);

            request.target = request_line.substr(
                first_space + 1,
                second_space - first_space - 1
            );

            request.version = request_line.substr(second_space + 1);

            if(!isValidMethod(request.method)) {
                return ParseResult::BadRequest;
            }

            if(!isValidTarget(request.target)) {
                return ParseResult::BadRequest;
            }

            if(request.version != "HTTP/1.1") {
                return ParseResult::UnsupportedVersion;
            }

            cursor = end + 2;
            state = State::Headers;
            continue;
        }


        if(state == State::Headers) {
            while(true) {
                if(cursor > buffer.size()) {
                    return ParseResult::BadRequest;
                }

                std::size_t end = buffer.find("\r\n", cursor);

                if(end == std::string::npos) {
                    if(buffer.size() - cursor > MAX_HEADER_LINE_SIZE) {
                        return ParseResult::HeadersTooLarge;
                    }

                    if(headers_size + (buffer.size() - cursor) > MAX_HEADERS_SIZE) {
                        return ParseResult::HeadersTooLarge;
                    }

                    return ParseResult::NeedMoreData;
                }

                std::size_t line_size = end - cursor;

                if(line_size > MAX_HEADER_LINE_SIZE) {
                    return ParseResult::HeadersTooLarge;
                }

                if(headers_size + line_size + 2 > MAX_HEADERS_SIZE) {
                    return ParseResult::HeadersTooLarge;
                }

                headers_size += line_size + 2;

                if(end == cursor) {
                    cursor = end + 2;

                    if(host_count != 1) {
                        return ParseResult::BadRequest;
                    }

                    if(has_transfer_encoding && has_content_length) {
                        return ParseResult::BadRequest;
                    }

                    if(has_transfer_encoding) {
                        if(!chunked_transfer_encoding) {
                            return ParseResult::UnsupportedTransferEncoding;
                        }

                        state = State::ChunkSize;
                    } else {
                        state = State::Body;
                    }

                    break;
                }

                header_count++;

                if(header_count > MAX_HEADER_COUNT) {
                    return ParseResult::TooManyHeaders;
                }

                std::string header_line = buffer.substr(
                    cursor,
                    line_size
                );

                std::size_t colon = header_line.find(':');

                if(colon == std::string::npos || colon == 0) {
                    return ParseResult::BadRequest;
                }

                std::string name = header_line.substr(
                    0,
                    colon
                );

                if(!isValidHeaderName(name)) {
                    return ParseResult::BadRequest;
                }

                std::size_t value_start = colon + 1;

                while(value_start < header_line.size() && (header_line[value_start] == ' ' || header_line[value_start] == '\t')) {
                    value_start++;
                }

                std::string value = header_line.substr(value_start);

                if(equalsIgnoreCase(name, "Host")) {
                    host_count++;

                    if(host_count > 1) {
                        return ParseResult::BadRequest;
                    }

                    if(value.empty()) {
                        return ParseResult::BadRequest;
                    }
                }

                if(equalsIgnoreCase(name, "Content-Length")) {
                    if(value.empty()) {
                        return ParseResult::BadRequest;
                    }

                    std::size_t length = 0;

                    for(char character : value) {
                        if(!std::isdigit(static_cast<unsigned char>(character))) {
                            return ParseResult::BadRequest;
                        }

                        std::size_t digit = character - '0';

                        if(length > (MAX_BODY_SIZE - digit) / 10) {
                            return ParseResult::BodyTooLarge;
                        }

                        length = length * 10 + digit;
                    }

                    if(has_content_length && content_length != length) {
                        return ParseResult::BadRequest;
                    }

                    content_length = length;
                    has_content_length = true;
                }

                if(equalsIgnoreCase(name, "Transfer-Encoding")) {
                    if(has_transfer_encoding) {
                        return ParseResult::BadRequest;
                    }

                    std::string transfer_encoding = trim(value);

                    if(!equalsIgnoreCase(transfer_encoding, "chunked")) {
                        return ParseResult::UnsupportedTransferEncoding;
                    }

                    has_transfer_encoding = true;
                    chunked_transfer_encoding = true;
                }

                request.headers.push_back({name, value});

                cursor = end + 2;
            }

            continue;
        }


        if(state == State::Body) {
            if(!has_content_length || content_length == 0) {
                return ParseResult::Complete;
            }

            if(content_length > MAX_BODY_SIZE) {
                return ParseResult::BodyTooLarge;
            }

            if(buffer.size() < cursor) {
                return ParseResult::BadRequest;
            }

            std::size_t available_body_bytes = buffer.size() - cursor;

            if(available_body_bytes == 0) {
                return ParseResult::NeedMoreData;
            }

            std::size_t remaining_body_bytes = content_length - request.body.size();

            std::size_t bytes_to_consume = available_body_bytes;

            if(bytes_to_consume > remaining_body_bytes) {
                bytes_to_consume = remaining_body_bytes;
            }

            request.body.append(
                buffer,
                cursor,
                bytes_to_consume
            );

            cursor += bytes_to_consume;

            if(request.body.size() < content_length) {
                return ParseResult::NeedMoreData;
            }

            return ParseResult::Complete;
        }


        if(state == State::ChunkSize) {
            std::size_t end = buffer.find("\r\n", cursor);

            if(end == std::string::npos) {
                if(buffer.size() - cursor > MAX_HEADER_LINE_SIZE) {
                    return ParseResult::HeadersTooLarge;
                }

                return ParseResult::NeedMoreData;
            }

            std::size_t line_size = end - cursor;

            if(line_size > MAX_HEADER_LINE_SIZE) {
                return ParseResult::HeadersTooLarge;
            }

            std::string chunk_size_line = buffer.substr(
                cursor,
                line_size
            );

            std::size_t parsed_size = 0;

            if(!isValidChunkSize(chunk_size_line, parsed_size)) {
                return ParseResult::BadRequest;
            }

            cursor = end + 2;

            chunk_size = parsed_size;
            chunk_bytes_read = 0;

            if(chunk_size == 0) {
                state = State::ChunkTrailers;
            } else {
                state = State::ChunkData;
            }

            continue;
        }


        if(state == State::ChunkData) {
            if(buffer.size() < cursor) {
                return ParseResult::BadRequest;
            }

            std::size_t available_body_bytes = buffer.size() - cursor;
            std::size_t remaining_chunk_bytes = chunk_size - chunk_bytes_read;

            if(available_body_bytes == 0) {
                return ParseResult::NeedMoreData;
            }

            std::size_t bytes_to_consume = available_body_bytes;

            if(bytes_to_consume > remaining_chunk_bytes) {
                bytes_to_consume = remaining_chunk_bytes;
            }

            if(request.body.size() + bytes_to_consume > MAX_BODY_SIZE) {
                return ParseResult::BodyTooLarge;
            }

            request.body.append(
                buffer,
                cursor,
                bytes_to_consume
            );

            cursor += bytes_to_consume;
            chunk_bytes_read += bytes_to_consume;

            if(chunk_bytes_read < chunk_size) {
                return ParseResult::NeedMoreData;
            }

            state = State::ChunkDataCRLF;
            continue;
        }


        if(state == State::ChunkDataCRLF) {
            if(buffer.size() - cursor < 2) {
                return ParseResult::NeedMoreData;
            }

            if(buffer[cursor] != '\r' || buffer[cursor + 1] != '\n') {
                return ParseResult::BadRequest;
            }

            cursor += 2;
            state = State::ChunkSize;
            continue;
        }


        if(state == State::ChunkTrailers) {
            while(true) {
                std::size_t end = buffer.find("\r\n", cursor);

                if(end == std::string::npos) {
                    if(buffer.size() - cursor > MAX_HEADER_LINE_SIZE) {
                        return ParseResult::HeadersTooLarge;
                    }

                    if(trailer_size + (buffer.size() - cursor) > MAX_HEADERS_SIZE) {
                        return ParseResult::HeadersTooLarge;
                    }

                    return ParseResult::NeedMoreData;
                }

                std::size_t line_size = end - cursor;

                if(trailer_size + line_size + 2 > MAX_HEADERS_SIZE) {
                    return ParseResult::HeadersTooLarge;
                }

                trailer_size += line_size + 2;

                if(end == cursor) {
                    cursor = end + 2;
                    return ParseResult::Complete;
                }

                trailer_count++;

                if(trailer_count > MAX_HEADER_COUNT) {
                    return ParseResult::TooManyHeaders;
                }

                std::string trailer_line = buffer.substr(
                    cursor,
                    line_size
                );

                std::size_t colon = trailer_line.find(':');

                if(colon == std::string::npos || colon == 0) {
                    return ParseResult::BadRequest;
                }

                std::string name = trailer_line.substr(
                    0,
                    colon
                );

                if(!isValidHeaderName(name)) {
                    return ParseResult::BadRequest;
                }

                cursor = end + 2;
            }
        }

        return ParseResult::NeedMoreData;
    }
}


const HttpRequest& HttpParser::getRequest() const {
    return request;
}


HttpParser::State HttpParser::getState() const {
    return state;
}


std::size_t HttpParser::getConsumedBytes() const {
    return cursor;
}


std::size_t HttpParser::consumeParsedBytes() {
    std::size_t consumed = cursor;

    cursor = 0;

    return consumed;
}


void HttpParser::reset() {
    state = State::RequestLine;

    request = HttpRequest{};

    cursor = 0;
    content_length = 0;
    headers_size = 0;
    header_count = 0;
    host_count = 0;

    chunk_size = 0;
    chunk_bytes_read = 0;
    trailer_size = 0;
    trailer_count = 0;

    has_content_length = false;
    has_transfer_encoding = false;
    chunked_transfer_encoding = false;
}