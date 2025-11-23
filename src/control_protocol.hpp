#ifndef CONTROL_PROTOCOL_HPP
#define CONTROL_PROTOCOL_HPP

#include "tls_wrapper.hpp"
#include "nlohmann/json.hpp"
#include <mutex>

using json = nlohmann::json;

class ControlProtocol {
public:
    enum class Role {
        NONE,
        HOST,
        CLIENT
    };

    enum class Mode {
        NONE,
        RESTRICTED,
        ADMIN
    };

    ControlProtocol(TLSWrapper& tls);

    bool negotiate_roles(Role my_role);
    Mode confirm_mode(bool is_host, bool requested_admin);
    void send_terminate();
    void handle_control_message();

private:
    void send_control_json(const json& msg);
    json receive_control_json();

    TLSWrapper& tls_;
    Role my_proposed_role_ = Role::NONE;
    Role peer_proposed_role_ = Role::NONE;
    std::mutex protocol_mutex_;
};

#endif // CONTROL_PROTOCOL_HPP