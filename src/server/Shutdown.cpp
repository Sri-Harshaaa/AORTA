#include "server/Shutdown.hpp"

namespace shutdown {

volatile std::sig_atomic_t requested = 0;

namespace {

void handleSignal(int signal) {
    if(signal == SIGINT || signal == SIGTERM) {
        requested = 1;
    }
}

}

void install() {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    /*
     * A client that disappears mid-response would otherwise kill the process.
     * Sends already pass MSG_NOSIGNAL, but sendfile() has no such flag.
     */
    std::signal(SIGPIPE, SIG_IGN);
}

bool isRequested() {
    return requested != 0;
}

}
