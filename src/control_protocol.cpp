#include "control_protocol.hpp"
#include "utils.hpp"
#include "framing.hpp"

ControlProtocol::ControlProtocol(TLSWrapper& tls) : tls_(tls) {}

bool ControlProtocol::negotiate_roles(Role my_role) {
    // Implementation of role negotiation
    return true;
}

ControlProtocol::Mode ControlProtocol::confirm_mode(bool is_host, bool requested_admin) {
    // Implementation of mode confirmation
    return Mode::RESTRICTED;
}

void ControlProtocol::send_terminate() {
    // Implementation of sending terminate message
}

void ControlProtocol::handle_control_message() {
    // Implementation of handling control messages
}

void ControlProtocol::send_control_json(const json& msg) {
    std::string msg_str = msg.dump();
    std::vector<uint8_t> payload(msg_str.begin(), msg_str.end());
    auto frame = framing::build_frame(framing::FrameType::CONTROL, payload);
    tls_.tls_write(frame.data(), frame.size());
}

json ControlProtocol::receive_control_json() {
    // This is a simplified implementation. A real implementation would need to handle framing and potential partial reads.
    unsigned char buffer[4096];
    int len = tls_.tls_read(buffer, sizeof(buffer) - 1);
    if (len > 0) {
        buffer[len] = '\0';
        return json::parse(buffer);
    }
    return json();
}