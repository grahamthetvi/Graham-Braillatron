#include "frame_protocol.h"

#include <cstring>

namespace braillatron::display {

namespace {

uint32_t crc32_update(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
        const uint32_t mask = -(crc & 1u);
        crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
    return crc;
}

} // namespace

uint32_t crc32_rgb565(const uint16_t *pixels, size_t count)
{
    uint32_t crc = 0xFFFFFFFFu;
    const auto *bytes = reinterpret_cast<const uint8_t *>(pixels);
    for (size_t i = 0; i < count * sizeof(uint16_t); ++i) {
        crc = crc32_update(crc, bytes[i]);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::vector<uint8_t> encode_frame_packet(const FrameHeader &header, const uint16_t *pixels,
                                         size_t pixel_count)
{
    const size_t payload_bytes = pixel_count * sizeof(uint16_t);
    std::vector<uint8_t> packet;
    packet.resize(4 + 1 + 4 + 2 + 2 + 4 + payload_bytes);

    size_t offset = 0;
    std::memcpy(packet.data() + offset, kFrameMagic, 4);
    offset += 4;
    packet[offset++] = kFrameProtocolVersion;
    std::memcpy(packet.data() + offset, &header.frame_id, sizeof(header.frame_id));
    offset += sizeof(header.frame_id);
    std::memcpy(packet.data() + offset, &header.width, sizeof(header.width));
    offset += sizeof(header.width);
    std::memcpy(packet.data() + offset, &header.height, sizeof(header.height));
    offset += sizeof(header.height);
    std::memcpy(packet.data() + offset, &header.crc32, sizeof(header.crc32));
    offset += sizeof(header.crc32);
    std::memcpy(packet.data() + offset, pixels, payload_bytes);
    return packet;
}

bool decode_frame_packet(const uint8_t *data, size_t size, FrameHeader &header,
                         std::vector<uint16_t> &pixels)
{
    if (size < 17 || std::memcmp(data, kFrameMagic, 4) != 0 || data[4] != kFrameProtocolVersion) {
        return false;
    }

    size_t offset = 5;
    std::memcpy(&header.frame_id, data + offset, sizeof(header.frame_id));
    offset += sizeof(header.frame_id);
    std::memcpy(&header.width, data + offset, sizeof(header.width));
    offset += sizeof(header.width);
    std::memcpy(&header.height, data + offset, sizeof(header.height));
    offset += sizeof(header.height);
    std::memcpy(&header.crc32, data + offset, sizeof(header.crc32));
    offset += sizeof(header.crc32);

    if (header.width == 0 || header.height == 0) {
        return false;
    }

    const size_t pixel_count = static_cast<size_t>(header.width) * header.height;
    const size_t expected = offset + pixel_count * sizeof(uint16_t);
    if (size < expected) {
        return false;
    }

    pixels.resize(pixel_count);
    std::memcpy(pixels.data(), data + offset, pixel_count * sizeof(uint16_t));
    if (header.crc32 != 0 &&
        crc32_rgb565(pixels.data(), pixel_count) != header.crc32) {
        return false;
    }
    return true;
}

} // namespace braillatron::display
