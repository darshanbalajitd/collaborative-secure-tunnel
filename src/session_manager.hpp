#pragma once

#include "app_config.hpp"
#include "control_protocol.hpp"
#include "listener.hpp"
#include "pty_handler.hpp"
#include "resize_coalescer.hpp"
#include "tls_wrapper.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

enum class SessionState {
    INITIAL,
    LISTENING,
    CONNECTING,
    HANDSHAKE,
    NEGOTIATING_ROLES,
    CONFIRMING_PRIVILEGES,
    SESSION_ACTIVE,
    SESSION_ENDED
};

class SessionManager {
public:
    SessionManager(const AppConfig& config);
    ~SessionManager();

    bool start();
    void stop();
    void wait_for_session();
    bool start_listening();
    bool connect_to_peer(const std::string& ip);

private:
    void connect_to_peer();
    void handle_connection(intptr_t fd);
    void run_session(intptr_t fd);
    void start_host_session();
    void start_non_host_session();
    void cleanup_session();

    void stdin_to_tls_loop();
    void tls_to_output_loop();
    void pty_to_tls_loop();

    void set_state(SessionState new_state);

    AppConfig config;
    SessionState state;
    std::mutex state_mutex;

    std::unique_ptr<Listener> listener;
    std::unique_ptr<TLSWrapper> tls_wrapper;
    std::unique_ptr<ControlProtocol> control_protocol;
    PTYHandler pty_handler;
    intptr_t pty_fd_ = -1;
    std::unique_ptr<ResizeCoalescer> resize_coalescer;

    ControlProtocol::Role role;
    ControlProtocol::Mode mode;

    std::atomic<bool> session_should_end;
    std::thread stdin_thread;
    std::thread tls_thread;
    std::thread pty_thread;
};