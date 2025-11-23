#ifndef RESIZE_COALESCER_HPP
#define RESIZE_COALESCER_HPP

#include "tls_wrapper.hpp"
#include <thread>
#include <mutex>
#include <condition_variable>

class ResizeCoalescer {
public:
    ResizeCoalescer(TLSWrapper& tls);
    ~ResizeCoalescer();

    void start();
    void stop();
    void signal_resize();

private:
    void coalescer_loop();
    void send_winch_frame(int rows, int cols);

    TLSWrapper& tls_;
    std::thread thread_;
    bool running_ = false;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool pending_resize_ = false;
};

#endif // RESIZE_COALESCER_HPP