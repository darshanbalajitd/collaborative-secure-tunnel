#include "session_manager.hpp"
#include "utils.hpp"
#include "io_bridge.hpp"
#include <iostream>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

SessionManager::SessionManager(const AppConfig& config) : config(config) {}

SessionManager::~SessionManager() {}

bool SessionManager::start_listening() {
    listener = std::make_unique<Listener>(config.port);
    if (!listener->start()) {
        return false;
    }
    return true;
}

bool SessionManager::connect_to_peer(const std::string& ip) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("WSAStartup failed");
        return false;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        LOG_ERROR("socket() failed: %d", WSAGetLastError());
        WSACleanup();
        return false;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(config.port));
    int pton = inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    if (pton != 1) {
        // Try resolving hostname
        addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM; hints.ai_protocol = IPPROTO_TCP;
        addrinfo* res = nullptr;
        int gai = getaddrinfo(ip.c_str(), nullptr, &hints, &res);
        if (gai != 0 || res == nullptr) {
            LOG_ERROR("getaddrinfo failed for %s", ip.c_str());
            closesocket(s);
            WSACleanup();
            return false;
        }
        sockaddr_in* a = reinterpret_cast<sockaddr_in*>(res->ai_addr);
        addr.sin_addr = a->sin_addr;
        freeaddrinfo(res);
    }

    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("connect() failed: %d", WSAGetLastError());
        closesocket(s);
        WSACleanup();
        return false;
    }

    intptr_t fd = static_cast<intptr_t>(s);
    run_session(fd);
    // Close after session (TLSWrapper handles close_notify if used)
    closesocket(s);
    WSACleanup();
    return true;
#else
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        LOG_ERROR("socket() failed");
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config.port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        LOG_ERROR("inet_pton failed for %s", ip.c_str());
        close(s);
        return false;
    }

    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR("connect() failed");
        close(s);
        return false;
    }

    run_session(static_cast<intptr_t>(s));
    close(s);
    return true;
#endif
}

void SessionManager::wait_for_session() {
    intptr_t fd = listener->accept_connection();
    if (fd != -1) {
        run_session(fd);
    }
}

void SessionManager::run_session(intptr_t fd) {
    tls_wrapper = std::make_unique<TLSWrapper>();
    tls_wrapper->set_verify_required(config.verify_required);
    if (!tls_wrapper->configure_ssl(config.mode == "listen", config.cert_path, config.key_path, config.ca_path)) {
        return;
    }

    if (!tls_wrapper->attach_socket(fd)) {
        return;
    }

    if (!tls_wrapper->perform_handshake()) {
        return;
    }

    std::cout << "TLS handshake successful" << std::endl;
    std::cout << "Peer fingerprint: " << tls_wrapper->get_peer_fingerprint() << std::endl;
    if (config.tls_info) {
        std::cout << "TLS version: " << tls_wrapper->get_tls_version() << std::endl;
        std::cout << "Cipher suite: " << tls_wrapper->get_ciphersuite() << std::endl;
    }
    resize_coalescer = std::make_unique<ResizeCoalescer>(*tls_wrapper);
    resize_coalescer->start();
    resize_coalescer->signal_resize();
    if (config.mode == "listen") {
        run_server_shell(*tls_wrapper, config.mirror_output, config.mirror_input, config.mirror_clean);
    } else {
        run_client_console(*tls_wrapper);
    }
}
