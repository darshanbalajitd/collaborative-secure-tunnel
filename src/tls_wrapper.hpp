#pragma once

#include <string>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"

class TLSWrapper {
public:
    TLSWrapper();
    TLSWrapper(const std::string& cert_file, const std::string& key_file, const std::string& ca_file);
    ~TLSWrapper();

    bool configure_ssl(bool is_server, const std::string& cert, const std::string& key, const std::string& ca);

    bool attach_socket(intptr_t fd);
    bool perform_handshake();
    int tls_write_all(const void* buf, size_t len);
    int tls_read_exact(void* buf, size_t len);
    int tls_write(const void* buf, size_t len);
    int tls_read(void* buf, size_t len);
    void close_notify();
    std::string get_peer_fingerprint();
    std::string get_tls_version();
    std::string get_ciphersuite();
    void set_verify_required(bool v) { verify_required_ = v; }

    intptr_t socket_fd() const { return socket_fd_; }

private:
    bool initialize_context();
    bool load_certificates();
    bool configure_ssl_internal(bool is_server);

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt srvcert;
    mbedtls_pk_context pkey;
    mbedtls_x509_crt cacert;

    std::string cert_file;
    std::string key_file;
    std::string ca_file;
    bool is_server_ = false;
    bool verify_required_ = false;

    mbedtls_net_context server_fd;

    intptr_t socket_fd_ = -1;
};
