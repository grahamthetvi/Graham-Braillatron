#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace braillatron::display {

constexpr char kFrameMagic[4] = {'B', 'R', 'D', 'F'};
constexpr uint8_t kFrameProtocolVersion = 1;

struct FrameHeader {
    uint32_t frame_id = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t crc32 = 0;
};

uint32_t crc32_rgb565(const uint16_t *pixels, size_t count);

std::vector<uint8_t> encode_frame_packet(const FrameHeader &header, const uint16_t *pixels,
                                         size_t pixel_count);

bool decode_frame_packet(const uint8_t *data, size_t size, FrameHeader &header,
                         std::vector<uint16_t> &pixels);

} // namespace braillatron::display
