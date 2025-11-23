#include "signal_handler.hpp"
#include "utils.hpp"
#include <csignal>
#include <cstdlib>

namespace {
    volatile std::sig_atomic_t g_signal_status;
}

void signal_handler(int signal) {
    g_signal_status = signal;
}

void setup_signal_handlers() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
}