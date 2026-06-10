#pragma once

#include "evdev_keymap.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct libevdev;

namespace braillatron::keyboard {

struct EvdevKeyEvent {
    unsigned code = 0;
    bool pressed = false;
};

class EvdevInput {
public:
    EvdevInput(std::string device_path, bool grab_device);
    ~EvdevInput();

    EvdevInput(const EvdevInput &) = delete;
    EvdevInput &operator=(const EvdevInput &) = delete;

    static std::string resolve_device_path(const std::string &configured_path);

    bool start(const EvdevKeymap &keymap);
    void stop();
    bool is_connected() const;

    void drain_events(std::vector<EvdevKeyEvent> &out);

    const std::string &device_path() const { return device_path_; }

private:
    std::string device_path_;
    bool grab_device_;
    EvdevKeymap keymap_;

    int fd_ = -1;
    libevdev *dev_ = nullptr;

    std::mutex queue_mutex_;
    std::vector<EvdevKeyEvent> pending_events_;

    std::thread worker_;
    std::atomic<bool> running_ {false};
    std::atomic<bool> connected_ {false};
};

} // namespace braillatron::keyboard
