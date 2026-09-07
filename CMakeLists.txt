cmake_minimum_required(VERSION 3.20)

project(AORTA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS_RELEASE
    "${CMAKE_CXX_FLAGS_RELEASE} -O2 -Wall -Wextra"
)

include_directories(include)

set(SERVER_SOURCES
    apps/server/main.cpp
    src/server/Server.cpp
    src/server/Reactor.cpp
    src/server/Metrics.cpp
    src/net/Socket.cpp
    src/net/Epoll.cpp
    src/net/Connection.cpp
    src/net/TimerFd.cpp
    src/http/HttpRequest.cpp
    src/http/HttpResponse.cpp
    src/http/HttpParser.cpp
    src/http/HttpRouter.cpp
    src/http/HttpHandler.cpp
    src/http/StaticFileHandler.cpp
    src/task/TaskManager.cpp
    src/redis/RedisClient.cpp
)

set(LB_SOURCES
    apps/lb/main.cpp
    src/lb/LoadBalancer.cpp
    src/lb/BackendPool.cpp
    src/lb/BackendConfig.cpp
    src/lb/HealthChecker.cpp
    src/lb/LbMetrics.cpp
    src/lb/ConsistentHash.cpp
    src/net/Socket.cpp
    src/net/Epoll.cpp
    src/net/TimerFd.cpp
)

add_executable(aorta
    ${SERVER_SOURCES}
)

target_link_libraries(aorta
    PRIVATE
    pthread
    hiredis
)

add_executable(aorta_lb
    ${LB_SOURCES}
)

target_link_libraries(aorta_lb
    PRIVATE
    pthread
)