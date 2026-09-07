#pragma once

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

/*
 * A deliberately tiny test harness.
 *
 * The project has no dependency manager, and pulling in GoogleTest to check a
 * parser would be a heavier commitment than the tests themselves. This is
 * enough: registration, assertions that print the values they compared, and a
 * non-zero exit code when something fails, which is all CI needs.
 */
namespace test {

struct Case {
    std::string name;
    std::function<void()> body;
};

inline std::vector<Case>& cases() {
    static std::vector<Case> registry;
    return registry;
}

inline int& failureCount() {
    static int count = 0;
    return count;
}

inline bool& currentFailed() {
    static bool failed = false;
    return failed;
}

struct Register {
    Register(const std::string& name, std::function<void()> body) {
        cases().push_back({name, std::move(body)});
    }
};

inline void reportFailure(
    const char* file,
    int line,
    const std::string& message
) {
    currentFailed() = true;
    ++failureCount();

    std::printf("    %s:%d  %s\n", file, line, message.c_str());
}

template <typename T>
std::string show(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

inline std::string show(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out += '"';

    for(char character : value) {
        if(character == '\r') {
            out += "\\r";
        } else if(character == '\n') {
            out += "\\n";
        } else {
            out += character;
        }
    }

    out += '"';
    return out;
}

inline int run(const char* suite) {
    std::printf("%s\n", suite);

    int passed = 0;

    for(const Case& test_case : cases()) {

        currentFailed() = false;

        test_case.body();

        if(currentFailed()) {
            std::printf("  FAIL  %s\n", test_case.name.c_str());
        } else {
            std::printf("  ok    %s\n", test_case.name.c_str());
            ++passed;
        }
    }

    std::printf(
        "\n%d/%zu passed\n",
        passed,
        cases().size()
    );

    return failureCount() == 0 ? 0 : 1;
}

}

#define TEST(name)                                                    \
    static void name();                                               \
    static ::test::Register register_##name(#name, name);             \
    static void name()

#define CHECK(condition)                                              \
    do {                                                              \
        if(!(condition)) {                                            \
            ::test::reportFailure(                                    \
                __FILE__, __LINE__, "expected " #condition);          \
        }                                                             \
    } while(0)

#define CHECK_EQ(actual, expected)                                    \
    do {                                                              \
        const auto& actual_value = (actual);                          \
        const auto& expected_value = (expected);                      \
        if(!(actual_value == expected_value)) {                       \
            ::test::reportFailure(                                    \
                __FILE__, __LINE__,                                   \
                std::string(#actual) + " was "                        \
                    + ::test::show(actual_value)                      \
                    + ", expected "                                   \
                    + ::test::show(expected_value));                  \
        }                                                             \
    } while(0)

#define TEST_MAIN(suite)                                              \
    int main() { return ::test::run(suite); }
