#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace braillatron::keyboard {

using MatrixStateHandler = std::function<void(uint16_t key_state)>;
using SerialDisconnectHandler = std::function<void()>;

class SerialListener {
public:
    SerialListener(std::string device_path, uint32_t baud_rate);
    ~SerialListener();

    SerialListener(const SerialListener &) = delete;
    SerialListener &operator=(const SerialListener &) = delete;

    bool start(MatrixStateHandler handler);
    void stop();
    bool is_connected() const;
    bool try_reconnect();

    void set_disconnect_handler(SerialDisconnectHandler handler);

private:
    std::string device_path_;
    uint32_t baud_rate_;
    int fd_ = -1;
    MatrixStateHandler handler_;
    SerialDisconnectHandler disconnect_handler_;
    std::thread worker_;
    std::atomic<bool> running_ {false};
    std::atomic<bool> connected_ {false};
};

} // namespace braillatron::keyboard
