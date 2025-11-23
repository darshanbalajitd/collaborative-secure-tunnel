#include "tls_wrapper.hpp"
#include "utils.hpp"
#include "mbedtls/sha256.h"
#include <cstring>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

TLSWrapper::TLSWrapper() {
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&srvcert);
    mbedtls_pk_init(&pkey);
    mbedtls_x509_crt_init(&cacert);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);
}

TLSWrapper::TLSWrapper(const std::string& cert_file, const std::string& key_file, const std::string& ca_file)
    : TLSWrapper() {
    // Default to server mode for this constructor; caller can reconfigure if needed
    configure_ssl(true, cert_file, key_file, ca_file);
}

TLSWrapper::~TLSWrapper() {
    mbedtls_x509_crt_free(&srvcert);
    mbedtls_pk_free(&pkey);
    mbedtls_x509_crt_free(&cacert);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
}

namespace {
    static int send_cb(void* ctx, const unsigned char* buf, size_t len) {
        TLSWrapper* self = static_cast<TLSWrapper*>(ctx);
        intptr_t fd = self->socket_fd();
        if (fd < 0) return MBEDTLS_ERR_NET_INVALID_CONTEXT;
        #ifdef _WIN32
        SOCKET s = (SOCKET)fd;
        int ret = ::send(s, reinterpret_cast<const char*>(buf), static_cast<int>(len), 0);
        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
            return MBEDTLS_ERR_NET_SEND_FAILED;
        }
        return ret;
        #else
        int ret = ::send(static_cast<int>(fd), buf, static_cast<int>(len), 0);
        if (ret < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) return MBEDTLS_ERR_SSL_WANT_WRITE;
            return MBEDTLS_ERR_NET_SEND_FAILED;
        }
        return ret;
        #endif
    }

    static int recv_cb(void* ctx, unsigned char* buf, size_t len) {
        TLSWrapper* self = static_cast<TLSWrapper*>(ctx);
        intptr_t fd = self->socket_fd();
        if (fd < 0) return MBEDTLS_ERR_NET_INVALID_CONTEXT;
        #ifdef _WIN32
        SOCKET s = (SOCKET)fd;
        int ret = ::recv(s, reinterpret_cast<char*>(buf), static_cast<int>(len), 0);
        if (ret == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
            return MBEDTLS_ERR_NET_RECV_FAILED;
        }
        if (ret == 0) return 0; // closed
        return ret;
        #else
        int ret = ::recv(static_cast<int>(fd), buf, static_cast<int>(len), 0);
        if (ret < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) return MBEDTLS_ERR_SSL_WANT_READ;
            return MBEDTLS_ERR_NET_RECV_FAILED;
        }
        return ret;
        #endif
    }
}

bool TLSWrapper::attach_socket(intptr_t fd) {
    socket_fd_ = fd;
    mbedtls_ssl_set_bio(&ssl, this, send_cb, recv_cb, nullptr);
    return true;
}

bool TLSWrapper::perform_handshake() {
    int ret;
    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            LOG_ERROR("mbedtls_ssl_handshake returned -0x%x", -ret);
            return false;
        }
    }
    return true;
}

int TLSWrapper::tls_write_all(const void* buf, size_t len) {
    int ret;
    const unsigned char* p = (const unsigned char*)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ret = mbedtls_ssl_write(&ssl, p, remaining);
        if (ret > 0) {
            p += ret;
            remaining -= ret;
        } else if (ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_WANT_READ) {
            LOG_ERROR("mbedtls_ssl_write returned -0x%x", -ret);
            return ret;
        }
    }
    return len;
}

int TLSWrapper::tls_read_exact(void* buf, size_t len) {
    int ret;
    unsigned char* p = (unsigned char*)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ret = mbedtls_ssl_read(&ssl, p, remaining);
        if (ret > 0) {
            p += ret;
            remaining -= ret;
        } else if (ret == 0) {
            // Connection closed
            return 0;
        } else if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            LOG_ERROR("mbedtls_ssl_read returned -0x%x", -ret);
            return ret;
        }
    }
    return len;
}

void TLSWrapper::close_notify() {
    mbedtls_ssl_close_notify(&ssl);
}

int TLSWrapper::tls_write(const void* buf, size_t len) {
    return mbedtls_ssl_write(&ssl, static_cast<const unsigned char*>(buf), static_cast<int>(len));
}

int TLSWrapper::tls_read(void* buf, size_t len) {
    return mbedtls_ssl_read(&ssl, static_cast<unsigned char*>(buf), static_cast<int>(len));
}

bool TLSWrapper::initialize_context() {
    const char* pers = "secure-tunnel";
    if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char*)pers, static_cast<size_t>(std::strlen(pers))) != 0) {
        LOG_ERROR("mbedtls_ctr_drbg_seed failed");
        return false;
    }

    if (!load_certificates()) {
        return false;
    }

    return true;
}

bool TLSWrapper::load_certificates() {
    if (is_server_) {
        if (mbedtls_x509_crt_parse_file(&srvcert, cert_file.c_str()) != 0) {
            LOG_ERROR("mbedtls_x509_crt_parse_file (cert) failed");
            return false;
        }

        if (mbedtls_pk_parse_keyfile(&pkey, key_file.c_str(), nullptr, mbedtls_ctr_drbg_random, &ctr_drbg) != 0) {
            LOG_ERROR("mbedtls_pk_parse_keyfile failed");
            return false;
        }
    }

    if (!ca_file.empty()) {
        if (mbedtls_x509_crt_parse_file(&cacert, ca_file.c_str()) != 0) {
            LOG_ERROR("mbedtls_x509_crt_parse_file (ca) failed");
            return false;
        }
    }
    return true;
}

bool TLSWrapper::configure_ssl_internal(bool is_server) {
    if (mbedtls_ssl_config_defaults(&conf,
                                    is_server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        LOG_ERROR("mbedtls_ssl_config_defaults failed");
        return false;
    }

    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    if (!ca_file.empty()) {
        mbedtls_ssl_conf_ca_chain(&conf, &cacert, nullptr);
        mbedtls_ssl_conf_authmode(&conf, verify_required_ ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_OPTIONAL);
    } else {
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    }

    if (is_server_) {
        if (mbedtls_ssl_conf_own_cert(&conf, &srvcert, &pkey) != 0) {
            LOG_ERROR("mbedtls_ssl_conf_own_cert failed");
            return false;
        }
    }

    if (mbedtls_ssl_setup(&ssl, &conf) != 0) {
        LOG_ERROR("mbedtls_ssl_setup failed");
        return false;
    }

    return true;
}

bool TLSWrapper::configure_ssl(bool is_server, const std::string& cert, const std::string& key, const std::string& ca) {
    cert_file = cert;
    key_file = key;
    ca_file = ca;
    is_server_ = is_server;

    if (!initialize_context()) {
        return false;
    }
    return configure_ssl_internal(is_server);
}

std::string TLSWrapper::get_peer_fingerprint() {
    const mbedtls_x509_crt* peer = mbedtls_ssl_get_peer_cert(&ssl);
    if (!peer) return std::string();

    unsigned char hash[32] = {0};
    if (mbedtls_sha256(peer->raw.p, peer->raw.len, hash, 0) != 0) {
        return std::string();
    }
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.resize(32 * 2);
    for (size_t i = 0; i < 32; ++i) {
        out[2*i] = hex[(hash[i] >> 4) & 0xF];
        out[2*i + 1] = hex[hash[i] & 0xF];
    }
    return out;
}

std::string TLSWrapper::get_tls_version() {
    const char* v = mbedtls_ssl_get_version(&ssl);
    if (!v) return std::string();
    return std::string(v);
}

std::string TLSWrapper::get_ciphersuite() {
    const char* s = mbedtls_ssl_get_ciphersuite(&ssl);
    if (!s) return std::string();
    return std::string(s);
}
