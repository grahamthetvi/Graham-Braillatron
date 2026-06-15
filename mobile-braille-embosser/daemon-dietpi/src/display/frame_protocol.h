#pragma once

#include <cstdint>
#include <vector>

namespace braillatron::display {

constexpr uint32_t kFrameMagic = 0x42524131; // "BRA1"

struct FrameHeader {
    uint32_t magic = kFrameMagic;
    uint32_t frame_id = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t payload_bytes = 0;
};

bool validate_frame_header(const FrameHeader &header);
size_t frame_packet_size(const FrameHeader &header);

} // namespace braillatron::display
