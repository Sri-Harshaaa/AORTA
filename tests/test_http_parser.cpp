#include "TestRunner.hpp"

#include "http/HttpParser.hpp"

#include <ostream>
#include <string>

using Result = HttpParser::ParseResult;

/*
 * Found by ADL from test::show, so a failed comparison prints the name of the
 * result rather than an integer.
 */
inline std::ostream& operator<<(std::ostream& out, Result result) {
    switch(result) {
        case Result::NeedMoreData:  return out << "NeedMoreData";
        case Result::Complete:      return out << "Complete";
        case Result::BadRequest:    return out << "BadRequest";
        case Result::RequestLineTooLarge: return out << "RequestLineTooLarge";
        case Result::HeadersTooLarge:     return out << "HeadersTooLarge";
        case Result::TooManyHeaders:      return out << "TooManyHeaders";
        case Result::BodyTooLarge:        return out << "BodyTooLarge";
        case Result::UnsupportedTransferEncoding:
            return out << "UnsupportedTransferEncoding";
        case Result::UnsupportedVersion:  return out << "UnsupportedVersion";
    }

    return out << "unknown";
}

namespace {

/*
 * Mirrors what Connection does: append to a buffer, parse, and erase whatever
 * the parser consumed so its cursor stays rebased on the front of the buffer.
 */
class Feeder {

public:
    Result feed(const std::string& chunk) {
        buffer += chunk;

        last = parser.parse(buffer);

        const std::size_t consumed = parser.consumeParsedBytes();

        if(consumed > 0) {
            buffer.erase(0, consumed);
        }

        return last;
    }

    /* Delivers the text one byte at a time, which is what a slow or hostile
     * client actually does and where naive parsers fall apart. */
    Result feedByteByByte(const std::string& text) {
        Result result = Result::NeedMoreData;

        for(char character : text) {
            result = feed(std::string(1, character));

            if(result != Result::NeedMoreData) {
                return result;
            }
        }

        return result;
    }

    const HttpRequest& request() const {
        return parser.getRequest();
    }

    void reset() {
        parser.reset();
    }

    const std::string& remaining() const {
        return buffer;
    }

private:
    HttpParser parser;
    std::string buffer;
    Result last{Result::NeedMoreData};
};

const std::string SIMPLE_GET =
    "GET /hello HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "\r\n";

}


TEST(parses_a_simple_get) {
    Feeder feeder;

    CHECK_EQ(feeder.feed(SIMPLE_GET), Result::Complete);
    CHECK_EQ(feeder.request().method, std::string("GET"));
    CHECK_EQ(feeder.request().target, std::string("/hello"));
}


TEST(parses_when_delivered_one_byte_at_a_time) {
    Feeder feeder;

    CHECK_EQ(feeder.feedByteByByte(SIMPLE_GET), Result::Complete);
    CHECK_EQ(feeder.request().target, std::string("/hello"));
}


TEST(parses_a_body_with_content_length) {
    Feeder feeder;

    const Result result = feeder.feed(
        "POST /tasks HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "title=hello"
    );

    CHECK_EQ(result, Result::Complete);
    CHECK_EQ(feeder.request().body, std::string("title=hello"));
}


TEST(waits_for_a_body_that_has_not_arrived) {
    Feeder feeder;

    CHECK_EQ(
        feeder.feed(
            "POST /tasks HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Length: 11\r\n"
            "\r\n"
            "title="
        ),
        Result::NeedMoreData
    );

    CHECK_EQ(feeder.feed("hello"), Result::Complete);
    CHECK_EQ(feeder.request().body, std::string("title=hello"));
}


TEST(parses_chunked_encoding) {
    Feeder feeder;

    const Result result = feeder.feed(
        "POST /tasks HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nhello\r\n"
        "6\r\n world\r\n"
        "0\r\n\r\n"
    );

    CHECK_EQ(result, Result::Complete);
    CHECK_EQ(feeder.request().body, std::string("hello world"));
}


TEST(parses_chunked_encoding_with_trailers) {
    Feeder feeder;

    const Result result = feeder.feed(
        "POST /tasks HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "4\r\nbody\r\n"
        "0\r\n"
        "X-Checksum: abc\r\n"
        "\r\n"
    );

    CHECK_EQ(result, Result::Complete);
    CHECK_EQ(feeder.request().body, std::string("body"));
}


/*
 * Request smuggling defences. Each of these is a real CVE class: a proxy and
 * an origin server that disagree about where one request ends let an attacker
 * prepend bytes to somebody else's request.
 */

TEST(rejects_transfer_encoding_together_with_content_length) {
    Feeder feeder;

    CHECK_EQ(
        feeder.feed(
            "POST /tasks HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Length: 6\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
        ),
        Result::BadRequest
    );
}


TEST(rejects_conflicting_duplicate_content_length) {
    Feeder feeder;

    CHECK_EQ(
        feeder.feed(
            "POST /tasks HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Length: 6\r\n"
            "Content-Length: 12\r\n"
            "\r\n"
        ),
        Result::BadRequest
    );
}


TEST(accepts_repeated_but_identical_content_length) {
    Feeder feeder;

    CHECK_EQ(
        feeder.feed(
            "POST /tasks HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Length: 4\r\n"
            "Content-Length: 4\r\n"
            "\r\n"
            "body"
        ),
        Result::Complete
    );
}


TEST(rejects_a_request_with_no_host) {
    Feeder feeder;

    CHECK_EQ(
        feeder.feed("GET /hello HTTP/1.1\r\n\r\n"),
        Result::BadRequest
    );
}


TEST(rejects_a_request_with_two_hosts) {
    Feeder feeder;

    CHECK_EQ(
        feeder.feed(
            "GET /hello HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Host: elsewhere\r\n"
            "\r\n"
        ),
        Result::BadRequest
    );
}


TEST(rejects_an_unsupported_transfer_encoding) {
    Feeder feeder;

    CHECK_EQ(
        feeder.feed(
            "POST /tasks HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Transfer-Encoding: gzip\r\n"
            "\r\n"
        ),
        Result::UnsupportedTransferEncoding
    );
}


TEST(rejects_an_unsupported_http_version) {
    Feeder feeder;

    CHECK_EQ(
        feeder.feed(
            "GET /hello HTTP/2.0\r\n"
            "Host: localhost\r\n"
            "\r\n"
        ),
        Result::UnsupportedVersion
    );
}


TEST(rejects_an_invalid_method) {
    Feeder feeder;

    CHECK_EQ(
        feeder.feed(
            "GET\x01 /hello HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n"
        ),
        Result::BadRequest
    );
}


TEST(rejects_too_many_headers) {
    std::string request =
        "GET /hello HTTP/1.1\r\n"
        "Host: localhost\r\n";

    for(int i = 0; i < 200; ++i) {
        request += "X-Pad-" + std::to_string(i) + ": v\r\n";
    }

    request += "\r\n";

    Feeder feeder;

    CHECK_EQ(feeder.feed(request), Result::TooManyHeaders);
}


TEST(rejects_an_oversized_request_line) {
    Feeder feeder;

    const std::string target(16 * 1024, 'a');

    CHECK_EQ(
        feeder.feed("GET /" + target + " HTTP/1.1\r\n"),
        Result::RequestLineTooLarge
    );
}


TEST(rejects_an_oversized_body) {
    Feeder feeder;

    CHECK_EQ(
        feeder.feed(
            "POST /tasks HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Length: 99999999\r\n"
            "\r\n"
        ),
        Result::BodyTooLarge
    );
}


TEST(rejects_a_content_length_that_would_overflow) {
    Feeder feeder;

    CHECK_EQ(
        feeder.feed(
            "POST /tasks HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Length: 99999999999999999999999999\r\n"
            "\r\n"
        ),
        Result::BodyTooLarge
    );
}


TEST(handles_two_pipelined_requests_in_one_read) {
    Feeder feeder;

    CHECK_EQ(feeder.feed(SIMPLE_GET + SIMPLE_GET), Result::Complete);

    feeder.reset();

    // The second request is still sitting in the buffer, unparsed.
    CHECK(!feeder.remaining().empty());
    CHECK_EQ(feeder.feed(""), Result::Complete);
}


TEST(reset_allows_the_parser_to_be_reused) {
    Feeder feeder;

    CHECK_EQ(feeder.feed(SIMPLE_GET), Result::Complete);

    feeder.reset();

    CHECK_EQ(
        feeder.feed(
            "GET /second HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n"
        ),
        Result::Complete
    );

    CHECK_EQ(feeder.request().target, std::string("/second"));
}


TEST_MAIN("HttpParser")
