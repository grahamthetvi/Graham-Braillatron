#pragma once

extern "C" {
#include "protocol.h"
}

#include <cstdint>
#include <string>

namespace braillatron::platform {

class SerialLink {
public:
    SerialLink(std::string device_path, uint32_t baud_rate);
    ~SerialLink();

    SerialLink(const SerialLink &) = delete;
    SerialLink &operator=(const SerialLink &) = delete;

    bool is_open() const;
    bool try_open();
    void close();
    bool send_heartbeat();

private:
    bool write_frame(uint8_t opcode, const void *payload, uint8_t payload_len);

    std::string device_path_;
    uint32_t baud_rate_;
    int fd_ = -1;
    uint8_t sequence_id_ = 0;
};

} // namespace braillatron::platform
