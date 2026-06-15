#include "frame_protocol.h"

namespace braillatron::display {

bool validate_frame_header(const FrameHeader &header)
{
    if (header.magic != kFrameMagic) {
        return false;
    }
    if (header.width == 0 || header.height == 0) {
        return false;
    }
    const size_t expected = static_cast<size_t>(header.width) * header.height * sizeof(uint16_t);
    return header.payload_bytes == expected;
}

size_t frame_packet_size(const FrameHeader &header)
{
    return sizeof(FrameHeader) + header.payload_bytes;
}

} // namespace braillatron::display
