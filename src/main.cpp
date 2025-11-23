#include "app_config.hpp"
#include "session_manager.hpp"
#include "signal_handler.hpp"
#include "utils.hpp"
#include <iostream>
#include <filesystem>
#include <cstdlib>

int main(int argc, char* argv[]) {
    AppConfig config;
    // Basic argument parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--listen") {
            config.mode = "listen";
        } else if (arg == "--connect" && i + 1 < argc) {
            config.mode = "connect";
            config.connect_ip = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            config.port = std::stoi(argv[++i]);
        } else if (arg == "--cert" && i + 1 < argc) {
            config.cert_path = argv[++i];
        } else if (arg == "--key" && i + 1 < argc) {
            config.key_path = argv[++i];
        } else if (arg == "--cacert" && i + 1 < argc) {
            config.ca_path = argv[++i];
        } else if (arg == "--auto-cert") {
            config.auto_cert = true;
        } else if (arg == "--tls-info") {
            config.tls_info = true;
        } else if (arg == "--verify-required") {
            config.verify_required = true;
        } else if (arg == "--keytype" && i + 1 < argc) {
            config.key_type = argv[++i];
        } else if (arg == "--debug") {
            config.debug = true;
        } else if (arg == "--mirror-output") {
            config.mirror_output = true;
        } else if (arg == "--mirror-input") {
            config.mirror_input = true;
        } else if (arg == "--mirror") {
            config.mirror_output = true;
            config.mirror_input = true;
        } else if (arg == "--mirror-clean") {
            config.mirror_clean = true;
        }
    }

    if (!config.validate()) {
        return 1;
    }

    initialize_logging("secure_tunnel.log", config.debug);
    setup_signal_handlers();

    auto file_exists = [](const std::string& p) { return !p.empty() && std::filesystem::exists(std::filesystem::path(p)); };
    if (config.auto_cert) {
        if (config.cert_path.empty()) config.cert_path = "cert.pem";
        if (config.key_path.empty()) config.key_path = "key.pem";
        bool need = !file_exists(config.cert_path) || !file_exists(config.key_path);
        if (need) {
            std::string cmd;
            if (config.key_type == "ecdsa") {
                cmd = std::string("openssl ecparam -name prime256v1 -genkey -noout -out ") + config.key_path +
                      std::string(" && openssl req -x509 -new -key ") + config.key_path + std::string(" -out ") + config.cert_path +
                      std::string(" -days 365 -nodes -subj \"/CN=localhost\"");
            } else {
                cmd = std::string("openssl req -x509 -newkey rsa:2048 -keyout ") + config.key_path + std::string(" -out ") + config.cert_path +
                      std::string(" -days 365 -nodes -subj \"/CN=localhost\"");
            }
            int rc = std::system(cmd.c_str());
            if (rc != 0) {
                LOG_ERROR("certificate generation failed");
            }
        }
    }

    SessionManager session_manager(config);

    if (config.mode == "listen") {
        if (session_manager.start_listening()) {
            session_manager.wait_for_session();
        }
    } else if (config.mode == "connect") {
        if (session_manager.connect_to_peer(config.connect_ip)) {
            // Session logic for client
        }
    }

    return 0;
}
