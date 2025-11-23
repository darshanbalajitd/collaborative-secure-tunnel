#include "framing.hpp"

namespace framing {

static void write_be32(uint32_t v, uint8_t out[4]) {
    out[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
    out[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    out[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    out[3] = static_cast<uint8_t>(v & 0xFF);
}

std::vector<uint8_t> build_frame(FrameType type, const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> frame;
    frame.reserve(1 + 4 + payload.size());

    frame.push_back(static_cast<uint8_t>(type));
    uint8_t len[4];
    write_be32(static_cast<uint32_t>(payload.size()), len);
    frame.insert(frame.end(), len, len + 4);
    frame.insert(frame.end(), payload.begin(), payload.end());

    return frame;
}

}