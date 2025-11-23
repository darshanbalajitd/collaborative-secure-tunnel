#ifndef PTY_HANDLER_HPP
#define PTY_HANDLER_HPP

#ifdef _WIN32

class PTYHandler {
public:
    PTYHandler();
    ~PTYHandler();

    bool create_pty_and_fork_shell();
    int get_master_fd() const;
    int get_child_pid() const;
    long pty_read_nonblocking(char* buf, size_t buf_size);
    long pty_write(const char* buf, size_t len);
    void apply_window_size(int rows, int cols);
    void wait_for_child();
    void terminate_child();

private:
    void configure_child_terminal();
    void execute_shell();

    void* pseudo_console_ = nullptr;
    void* in_write_ = nullptr;
    void* out_read_ = nullptr;
    unsigned long child_pid_ = 0;
};

#else

#include <termios.h>
#include <unistd.h>

class PTYHandler {
public:
    PTYHandler();
    ~PTYHandler();

    bool create_pty_and_fork_shell();
    int get_master_fd() const;
    pid_t get_child_pid() const;
    ssize_t pty_read_nonblocking(char* buf, size_t buf_size);
    ssize_t pty_write(const char* buf, size_t len);
    void apply_window_size(int rows, int cols);
    void wait_for_child();
    void terminate_child();

private:
    void configure_child_terminal();
    void execute_shell();

    int master_fd_ = -1;
    pid_t child_pid_ = -1;
    struct termios original_termios_;
};

#endif

#endif // PTY_HANDLER_HPP
