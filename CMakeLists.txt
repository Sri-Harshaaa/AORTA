cmake_minimum_required(VERSION 3.20)

project(AORTA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Benchmark numbers from an unoptimised build are meaningless, so default to
# Release rather than to CMake's empty build type.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra)

include_directories(include)

# --------------------------------------------------------------------------
# Shared pieces
# --------------------------------------------------------------------------

set(NET_SOURCES
    src/net/Socket.cpp
    src/net/Epoll.cpp
    src/net/TimerFd.cpp
)

set(HTTP_SOURCES
    src/http/HttpRequest.cpp
    src/http/HttpResponse.cpp
    src/http/HttpParser.cpp
    src/http/HttpRouter.cpp
    src/http/HttpHandler.cpp
    src/http/StaticFileHandler.cpp
)

# --------------------------------------------------------------------------
# aorta - backend HTTP server (epoll and threaded models in one binary)
# --------------------------------------------------------------------------

add_executable(aorta
    apps/server/main.cpp

    src/server/Server.cpp
    src/server/ThreadedServer.cpp
    src/server/Reactor.cpp
    src/server/Metrics.cpp
    src/server/MetricsRegistry.cpp
    src/server/Histogram.cpp
    src/server/ProcStats.cpp
    src/server/ServerConfig.cpp
    src/server/Shutdown.cpp

    src/net/Connection.cpp
    ${NET_SOURCES}
    ${HTTP_SOURCES}

    src/task/TaskManager.cpp
    src/task/PooledTaskStore.cpp
    src/redis/RedisClient.cpp
)

target_link_libraries(aorta PRIVATE pthread hiredis)

# --------------------------------------------------------------------------
# aorta_lb - layer 4 TCP load balancer
# --------------------------------------------------------------------------

add_executable(aorta_lb
    apps/lb/main.cpp

    src/lb/LoadBalancer.cpp
    src/lb/BackendPool.cpp
    src/lb/BackendConfig.cpp
    src/lb/HealthChecker.cpp
    src/lb/LbMetrics.cpp
    src/lb/LbConfig.cpp
    src/lb/ConsistentHash.cpp
    src/lb/BalancingStrategy.cpp

    ${NET_SOURCES}
)

target_link_libraries(aorta_lb PRIVATE pthread)

# --------------------------------------------------------------------------
# Tests - no external framework, run with ctest
# --------------------------------------------------------------------------

enable_testing()

add_executable(test_http_parser
    tests/test_http_parser.cpp
    src/http/HttpParser.cpp
    src/http/HttpRequest.cpp
)
target_include_directories(test_http_parser PRIVATE tests)

add_executable(test_histogram
    tests/test_histogram.cpp
    src/server/Histogram.cpp
)
target_include_directories(test_histogram PRIVATE tests)

add_executable(test_strategies
    tests/test_strategies.cpp
    src/lb/BalancingStrategy.cpp
    src/lb/ConsistentHash.cpp
    src/lb/BackendPool.cpp
)
target_include_directories(test_strategies PRIVATE tests)

add_test(NAME http_parser COMMAND test_http_parser)
add_test(NAME histogram COMMAND test_histogram)
add_test(NAME strategies COMMAND test_strategies)
