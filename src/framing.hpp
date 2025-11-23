#pragma once

#include <cstdint>
#include <vector>

namespace framing {

enum class FrameType : uint8_t {
    CONTROL = 1,
    DATA = 2
};

// Simple frame format:
// [type:1][len:4 big-endian][payload:len]
std::vector<uint8_t> build_frame(FrameType type, const std::vector<uint8_t>& payload);

}