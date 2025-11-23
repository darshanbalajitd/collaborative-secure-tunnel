#include "resize_coalescer.hpp"
#include "utils.hpp"
#include "framing.hpp"
#include "nlohmann/json.hpp"
#include <sys/ioctl.h>
#include <unistd.h>

using json = nlohmann::json;

ResizeCoalescer::ResizeCoalescer(TLSWrapper& tls) : tls_(tls) {}

ResizeCoalescer::~ResizeCoalescer() {
    stop();
}

void ResizeCoalescer::start() {
    running_ = true;
    thread_ = std::thread(&ResizeCoalescer::coalescer_loop, this);
}

void ResizeCoalescer::stop() {
    if (running_) {
        running_ = false;
        cv_.notify_one();
        if (thread_.joinable()) {
            thread_.join();
        }
    }
}

void ResizeCoalescer::signal_resize() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_resize_ = true;
    }
    cv_.notify_one();
}

void ResizeCoalescer::coalescer_loop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return pending_resize_ || !running_; });

        if (!running_) {
            break;
        }

        pending_resize_ = false;
        lock.unlock();

        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
            send_winch_frame(ws.ws_row, ws.ws_col);
        }
    }
}

void ResizeCoalescer::send_winch_frame(int rows, int cols) {
    json winch_msg = {
        {"type", "winch"},
        {"rows", rows},
        {"cols", cols}
    };
    std::string msg_str = winch_msg.dump();
    std::vector<uint8_t> payload(msg_str.begin(), msg_str.end());
    auto frame = framing::build_frame(framing::FrameType::CONTROL, payload);
    tls_.tls_write(frame.data(), frame.size());
}
