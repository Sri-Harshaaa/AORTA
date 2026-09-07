#pragma once

#include <csignal>

/*
 * Shared shutdown flag for the backend server.
 *
 * Both concurrency models poll this: the reactors between epoll_wait() calls,
 * and the threaded accept loop between accepts. A signal handler may only
 * touch a volatile sig_atomic_t, which is why this is not a std::atomic.
 */
namespace shutdown {

extern volatile std::sig_atomic_t requested;

void install();

bool isRequested();

}
