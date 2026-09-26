#pragma once

extern "C" {
#include "protocol.h"
}

#include <cstdint>
#include <mutex>
#include <string>
#include <unistd.h>

namespace braillatron::platform {

// Exclusive owner of the Arduino USB CDC device (typically /dev/ttyACM0).
// SerialListener reads from this link; do not open the CDC path a second time.
class SerialLink {
public:
    SerialLink(std::string device_path, uint32_t baud_rate);
    ~SerialLink();

    SerialLink(const SerialLink &) = delete;
    SerialLink &operator=(const SerialLink &) = delete;

    const std::string &device_path() const { return device_path_; }

    bool is_open() const;
    bool try_open();
    void close();
    bool send_heartbeat();
    bool send_telemetry(const braillatron_telemetry_t &payload);
    /** Pi → Arduino BRAILLATRON_OP_CLEAR_FAULT (zero payload). */
    bool send_clear_fault();

    /** RX helpers for SerialListener. Do not close the fd from the reader. */
    int poll_readable(int timeout_ms);
    ssize_t read_bytes(void *buf, size_t len);

private:
    bool write_frame(uint8_t opcode, const void *payload, uint8_t payload_len);

    std::string device_path_;
    uint32_t baud_rate_;
    mutable std::mutex mutex_;
    int fd_ = -1;
    uint8_t sequence_id_ = 0;
};

} // namespace braillatron::platform
