#include "pty_handler.hpp"
#include "utils.hpp"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <pty.h>
#include <errno.h>

PTYHandler::PTYHandler() {
    if (tcgetattr(STDIN_FILENO, &original_termios_) != 0) {
        LOG_WARN("tcgetattr() failed: %s", error_to_string(errno).c_str());
    }
}

PTYHandler::~PTYHandler() {
    terminate_child();
    tcsetattr(STDIN_FILENO, TCSANOW, &original_termios_);
}

bool PTYHandler::create_pty_and_fork_shell() {
    child_pid_ = forkpty(&master_fd_, nullptr, nullptr, nullptr);
    if (child_pid_ < 0) {
        LOG_ERROR("forkpty() failed: %s", error_to_string(errno).c_str());
        return false;
    }

    if (child_pid_ == 0) { // Child process
        configure_child_terminal();
        execute_shell();
    }

    // Parent process
    int flags = fcntl(master_fd_, F_GETFL, 0);
    fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK);

    return true;
}

int PTYHandler::get_master_fd() const {
    return master_fd_;
}

pid_t PTYHandler::get_child_pid() const {
    return child_pid_;
}

ssize_t PTYHandler::pty_read_nonblocking(char* buf, size_t buf_size) {
    ssize_t n = read(master_fd_, buf, buf_size);
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return 0;
    }
    return n;
}

ssize_t PTYHandler::pty_write(const char* buf, size_t len) {
    return write(master_fd_, buf, len);
}

void PTYHandler::apply_window_size(int rows, int cols) {
    struct winsize ws;
    ws.ws_row = rows;
    ws.ws_col = cols;
    ioctl(master_fd_, TIOCSWINSZ, &ws);
}

void PTYHandler::wait_for_child() {
    int status;
    waitpid(child_pid_, &status, 0);
}

void PTYHandler::terminate_child() {
    if (child_pid_ > 0) {
        kill(child_pid_, SIGTERM);
        wait_for_child();
        child_pid_ = -1;
    }
}

void PTYHandler::configure_child_terminal() {
    struct termios term_settings;
    if (tcgetattr(STDIN_FILENO, &term_settings) == 0) {
        term_settings.c_lflag |= (ECHO | ICANON);
        term_settings.c_iflag |= ICRNL;
        tcsetattr(STDIN_FILENO, TCSANOW, &term_settings);
    }
}

void PTYHandler::execute_shell() {
    const char* shell = getenv("SHELL");
    if (!shell) {
        shell = "/bin/sh";
    }
    const char* home = getenv("HOME");
    if (home) {
        chdir(home);
    }
    execlp(shell, shell, nullptr);
    LOG_ERROR("execlp() failed: %s", error_to_string(errno).c_str());
    exit(1);
}
