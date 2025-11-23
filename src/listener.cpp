#include "listener.hpp"
#include "utils.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

Listener::Listener(int port) : port_(port) {}

Listener::~Listener() {
    if (listen_fd_ != -1) {
        close(listen_fd_);
    }
}

bool Listener::start() {
    struct sockaddr_in serv_addr;

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        LOG_ERROR("socket() failed: %s", error_to_string(errno).c_str());
        return false;
    }

    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("setsockopt() failed: %s", error_to_string(errno).c_str());
        return false;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port_);

    if (bind(listen_fd_, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        LOG_ERROR("bind() failed: %s", error_to_string(errno).c_str());
        return false;
    }

    if (listen(listen_fd_, 5) < 0) {
        LOG_ERROR("listen() failed: %s", error_to_string(errno).c_str());
        return false;
    }

    return true;
}

intptr_t Listener::accept_connection() {
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);
    intptr_t new_fd = accept(listen_fd_, (struct sockaddr*)&cli_addr, &clilen);
    if (new_fd < 0) {
        LOG_ERROR("accept() failed: %s", error_to_string(errno).c_str());
        return -1;
    }
    return new_fd;
}
