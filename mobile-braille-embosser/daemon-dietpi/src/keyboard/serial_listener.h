#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

extern "C" {
#include "protocol.h"
}

namespace braillatron::keyboard {

struct SerialFrame {
    uint8_t opcode = 0;
    size_t payload_len = 0;
    std::array<uint8_t, BRAILLATRON_FRAME_MAX_PAYLOAD> payload {};
};

using FrameHandler = std::function<void(const SerialFrame &frame)>;
using SerialDisconnectHandler = std::function<void()>;

class SerialListener {
public:
    SerialListener(std::string device_path, uint32_t baud_rate);
    ~SerialListener();

    SerialListener(const SerialListener &) = delete;
    SerialListener &operator=(const SerialListener &) = delete;

    bool start(FrameHandler handler);
    void stop();
    bool is_connected() const;
    bool try_reconnect();

    void set_disconnect_handler(SerialDisconnectHandler handler);

private:
    std::string device_path_;
    uint32_t baud_rate_;
    int fd_ = -1;
    FrameHandler handler_;
    SerialDisconnectHandler disconnect_handler_;
    std::thread worker_;
    std::atomic<bool> running_ {false};
    std::atomic<bool> connected_ {false};
};

} // namespace braillatron::keyboard
