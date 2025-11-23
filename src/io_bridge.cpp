#include "io_bridge.hpp"
#include "tls_wrapper.hpp"
#include "pty_handler.hpp"
#include "framing.hpp"
#include "nlohmann/json.hpp"
#include "utils.hpp"

#include <thread>
#include <vector>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

static std::vector<uint8_t> filter_ansi_for_cmd(const std::vector<uint8_t>& in);
static std::vector<uint8_t> make_clean_cmd_out(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        uint8_t c = in[i];
        if (c == 0x1B) {
            if (i + 1 >= in.size()) break;
            uint8_t n = in[i + 1];
            if (n == '[') {
                i += 2;
                while (i < in.size()) { uint8_t ch = in[i++]; if (ch >= 0x40 && ch <= 0x7E) break; }
                continue;
            } else if (n == ']') {
                i += 2; bool done = false;
                while (i < in.size() && !done) { uint8_t ch = in[i++]; if (ch == 0x07) done = true; else if (ch == 0x1B && i < in.size() && in[i] == '\\') { i++; done = true; } }
                continue;
            } else if (n == 'P') {
                i += 2; bool done = false;
                while (i < in.size() && !done) { uint8_t ch = in[i++]; if (ch == 0x1B && i < in.size() && in[i] == '\\') { i++; done = true; } }
                continue;
            } else {
                i += 2; while (i < in.size()) { uint8_t ch = in[i++]; if (ch >= 0x40 && ch <= 0x7E) break; }
                continue;
            }
        }
        if (c == 0x08) { out.push_back(0x08); out.push_back(0x20); out.push_back(0x08); i++; continue; }
        if (c == 9 || c == 10 || c == 13 || (c >= 32 && c <= 126)) { out.push_back(c); }
        i++;
    }
    return out;
}

static void pump_tls_to_stdout_framed(TLSWrapper& tls) {
    std::vector<unsigned char> header(5);
    std::vector<unsigned char> payload;
    for (;;) {
        int hr = tls.tls_read_exact(header.data(), header.size());
        if (hr <= 0) break;
        uint8_t type = header[0];
        uint32_t len = (header[1] << 24) | (header[2] << 16) | (header[3] << 8) | header[4];
        payload.resize(len);
        int pr = tls.tls_read_exact(payload.data(), len);
        if (pr <= 0) break;
        if (type == (uint8_t)framing::FrameType::DATA) {
            #ifdef _WIN32
            DWORD written = 0; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), payload.data(), (DWORD)payload.size(), &written, nullptr);
            #else
            write(STDOUT_FILENO, payload.data(), payload.size());
            #endif
        }
    }
}

static void pump_stdin_to_tls_framed(TLSWrapper& tls) {
    std::vector<unsigned char> buf(4096);
    for (;;) {
        #ifdef _WIN32
        DWORD readn = 0; if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), buf.data(), (DWORD)buf.size(), &readn, nullptr)) break; if (readn == 0) break;
        #else
        ssize_t readn = read(STDIN_FILENO, buf.data(), buf.size()); if (readn <= 0) break;
        #endif
        std::vector<uint8_t> payload(buf.begin(), buf.begin() + readn);
        auto frame = framing::build_frame(framing::FrameType::DATA, payload);
        int w = tls.tls_write((const void*)frame.data(), frame.size());
        if (w <= 0) break;
    }
}

static void pump_tls_to_pty_framed(TLSWrapper& tls, PTYHandler& pty) {
    std::vector<unsigned char> header(5);
    std::vector<unsigned char> payload;
    for (;;) {
        int hr = tls.tls_read_exact(header.data(), header.size());
        if (hr <= 0) break;
        uint8_t type = header[0];
        uint32_t len = (header[1] << 24) | (header[2] << 16) | (header[3] << 8) | header[4];
        payload.resize(len);
        int pr = tls.tls_read_exact(payload.data(), len);
        if (pr <= 0) break;
        if (type == (uint8_t)framing::FrameType::DATA) {
            pty.pty_write((const char*)payload.data(), payload.size());
        } else if (type == (uint8_t)framing::FrameType::CONTROL) {
            try {
                auto j = nlohmann::json::parse(std::string((char*)payload.data(), payload.size()));
                if (j.contains("type") && j["type"] == "winch") {
                    int rows = j.value("rows", 24);
                    int cols = j.value("cols", 80);
                    pty.apply_window_size(rows, cols);
                }
            } catch (...) {}
        }
    }
}

static void pump_pty_to_tls_framed(PTYHandler& pty, TLSWrapper& tls, bool mirror_output, bool mirror_clean) {
    std::vector<unsigned char> buf(4096);
    for (;;) {
        long r = pty.pty_read_nonblocking((char*)buf.data(), buf.size());
        if (r < 0) break;
        if (r == 0) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }
        std::vector<uint8_t> payload(buf.begin(), buf.begin() + r);
        if (mirror_output) {
            auto outbuf = mirror_clean ? make_clean_cmd_out(payload) : payload;
            if (!outbuf.empty()) {
                #ifdef _WIN32
                DWORD written = 0; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), outbuf.data(), (DWORD)outbuf.size(), &written, nullptr);
                #else
                write(STDOUT_FILENO, outbuf.data(), outbuf.size());
                #endif
            }
        }
        auto frame = framing::build_frame(framing::FrameType::DATA, payload);
        int w = tls.tls_write((const void*)frame.data(), frame.size());
        if (w <= 0) break;
    }
}

static void pump_stdin_to_pty(PTYHandler& pty) {
    std::vector<unsigned char> buf(4096);
    for (;;) {
        #ifdef _WIN32
        DWORD readn = 0; if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), buf.data(), (DWORD)buf.size(), &readn, nullptr)) break; if (readn == 0) break;
        #else
        ssize_t readn = read(STDIN_FILENO, buf.data(), buf.size()); if (readn <= 0) break;
        #endif
        pty.pty_write((const char*)buf.data(), (size_t)readn);
    }
}

void run_client_console(TLSWrapper& tls) {
    #ifdef _WIN32
    DWORD inMode = 0, outMode = 0;
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hIn) GetConsoleMode(hIn, &inMode);
    if (hOut) GetConsoleMode(hOut, &outMode);
    if (hIn) {
        DWORD newIn = (inMode | ENABLE_VIRTUAL_TERMINAL_INPUT) & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
        SetConsoleMode(hIn, newIn);
    }
    if (hOut) SetConsoleMode(hOut, outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
    #else
    struct termios orig_in{}; struct termios raw_in{};
    struct termios orig_out{}; struct termios raw_out{};
    if (tcgetattr(STDIN_FILENO, &orig_in) == 0) { raw_in = orig_in; cfmakeraw(&raw_in); tcsetattr(STDIN_FILENO, TCSANOW, &raw_in); }
    if (tcgetattr(STDOUT_FILENO, &orig_out) == 0) { raw_out = orig_out; cfmakeraw(&raw_out); tcsetattr(STDOUT_FILENO, TCSANOW, &raw_out); }
    #endif

    std::thread t1(pump_stdin_to_tls_framed, std::ref(tls));
    std::thread t2(pump_tls_to_stdout_framed, std::ref(tls));
    t1.join();
    tls.close_notify();
    t2.join();

    #ifndef _WIN32
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_in);
    tcsetattr(STDOUT_FILENO, TCSANOW, &orig_out);
    #endif
}

void run_server_shell(TLSWrapper& tls, bool mirror_output, bool mirror_input, bool mirror_clean) {
    #ifdef _WIN32
    if (mirror_output) {
        DWORD outMode = 0; HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut && GetConsoleMode(hOut, &outMode)) {
            SetConsoleMode(hOut, outMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT);
        }
    }
    if (mirror_input) {
        DWORD inMode = 0; HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        if (hIn && GetConsoleMode(hIn, &inMode)) {
            DWORD newMode = (inMode | ENABLE_VIRTUAL_TERMINAL_INPUT) & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
            SetConsoleMode(hIn, newMode);
        }
    }
    #endif
    #ifndef _WIN32
    struct termios orig_in{}; bool have_orig = false;
    if (mirror_input) {
        struct termios raw_in{};
        if (tcgetattr(STDIN_FILENO, &orig_in) == 0) {
            have_orig = true;
            raw_in = orig_in;
            cfmakeraw(&raw_in);
            raw_in.c_lflag &= ~(ECHO);
            tcsetattr(STDIN_FILENO, TCSANOW, &raw_in);
        }
    }
    #endif
    PTYHandler pty;
    if (!pty.create_pty_and_fork_shell()) return;
    std::thread t0;
    if (mirror_input) {
        t0 = std::thread(pump_stdin_to_pty, std::ref(pty));
    }
    std::thread t1(pump_tls_to_pty_framed, std::ref(tls), std::ref(pty));
    std::thread t2(pump_pty_to_tls_framed, std::ref(pty), std::ref(tls), mirror_output, mirror_clean);
    LOG_INFO("Session active; forwarding PTY output to client%s%s%s",
             mirror_output ? " (mirrored to server console)" : "",
             mirror_input ? "; server console input enabled" : "",
             mirror_clean ? "; server mirror cleaned" : "");
    t1.join();
    tls.close_notify();
    t2.join();
    pty.terminate_child();
    #ifndef _WIN32
    if (mirror_input && have_orig) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_in);
    }
    #endif
    if (mirror_input && t0.joinable()) {
        t0.detach();
    }
    LOG_INFO("Session ended");
}
static std::vector<uint8_t> filter_ansi_for_cmd(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        uint8_t c = in[i];
        if (c == 0x1B) {
            if (i + 1 >= in.size()) break;
            uint8_t n = in[i + 1];
            if (n == '[') {
                i += 2;
                while (i < in.size()) {
                    uint8_t ch = in[i++];
                    if (ch >= 0x40 && ch <= 0x7E) break;
                }
                continue;
            } else if (n == ']') {
                i += 2;
                bool done = false;
                while (i < in.size() && !done) {
                    uint8_t ch = in[i++];
                    if (ch == 0x07) done = true;
                    else if (ch == 0x1B && i < in.size() && in[i] == '\\') { i++; done = true; }
                }
                continue;
            } else if (n == 'P') {
                i += 2;
                bool done = false;
                while (i < in.size() && !done) {
                    uint8_t ch = in[i++];
                    if (ch == 0x1B && i < in.size() && in[i] == '\\') { i++; done = true; }
                }
                continue;
            } else {
                i += 2;
                while (i < in.size()) {
                    uint8_t ch = in[i++];
                    if (ch >= 0x40 && ch <= 0x7E) break;
                }
                continue;
            }
        }
        if (c == 9 || c == 10 || c == 13 || (c >= 32 && c <= 126)) {
            out.push_back(c);
        }
        i++;
    }
    return out;
}
static std::vector<uint8_t> filter_ansi_for_cmd(const std::vector<uint8_t>& in);
