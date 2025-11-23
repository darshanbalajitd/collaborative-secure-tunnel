#include "listener.hpp"
#include "utils.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

namespace {
    WSADATA g_wsaData;
    bool g_wsa_started = false;
}

Listener::Listener(int port) : port_(port) {}

Listener::~Listener() {
    if (listen_fd_ != -1) {
        SOCKET s = static_cast<SOCKET>(listen_fd_);
        closesocket(s);
    }
    if (g_wsa_started) {
        WSACleanup();
        g_wsa_started = false;
    }
}

bool Listener::start() {
    if (!g_wsa_started) {
        if (WSAStartup(MAKEWORD(2, 2), &g_wsaData) != 0) {
            LOG_ERROR("WSAStartup failed");
            return false;
        }
        g_wsa_started = true;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        LOG_ERROR("socket() failed: %d", WSAGetLastError());
        return false;
    }

    int opt = 1;
    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt)) == SOCKET_ERROR) {
        LOG_ERROR("setsockopt() failed: %d", WSAGetLastError());
        closesocket(s);
        return false;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("bind() failed: %d", WSAGetLastError());
        closesocket(s);
        return false;
    }

    if (listen(s, SOMAXCONN) == SOCKET_ERROR) {
        LOG_ERROR("listen() failed: %d", WSAGetLastError());
        closesocket(s);
        return false;
    }

    listen_fd_ = static_cast<intptr_t>(s);
    return true;
}

intptr_t Listener::accept_connection() {
    SOCKET s = static_cast<SOCKET>(listen_fd_);
    sockaddr_in cli_addr;
    int addrlen = sizeof(cli_addr);
    SOCKET client = accept(s, reinterpret_cast<sockaddr*>(&cli_addr), &addrlen);
    if (client == INVALID_SOCKET) {
        LOG_ERROR("accept() failed: %d", WSAGetLastError());
        return -1;
    }
    return static_cast<intptr_t>(client);
}