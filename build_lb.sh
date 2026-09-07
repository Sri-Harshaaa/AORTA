#!/bin/bash

set -e

g++ -std=c++20 -O2 -Wall -Wextra -pthread \
    apps/lb/main.cpp \
    src/lb/LoadBalancer.cpp \
    src/lb/BackendPool.cpp \
    src/lb/BackendConfig.cpp \
    src/lb/HealthChecker.cpp \
    src/net/Socket.cpp \
    src/net/Epoll.cpp \
    src/net/TimerFd.cpp \
    -Iinclude \
    -o aorta_lb