#ifndef APP_CONFIG_HPP
#define APP_CONFIG_HPP

#include <string>

struct AppConfig {
    std::string mode;
    std::string connect_ip;
    int port = 0;
    std::string cert_path;
    std::string key_path;
    std::string ca_path;
    bool auto_cert = false;
    bool tls_info = false;
    bool verify_required = false;
    std::string key_type = "ecdsa";
    bool debug = false;
    bool mirror_output = false;
    bool mirror_input = false;
    bool mirror_clean = false;

    bool validate() const {
        if (mode != "listen" && mode != "connect") {
            return false;
        }
        if (port == 0) {
            return false;
        }
        if (mode == "connect" && connect_ip.empty()) {
            return false;
        }
        return true;
    }
};

#endif // APP_CONFIG_HPP
