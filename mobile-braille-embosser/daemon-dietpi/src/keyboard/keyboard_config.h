#pragma once

#include <cstdint>
#include <string>

namespace braillatron::keyboard {

struct KeyboardConfig {
    std::string hardware_config_path;
    std::string matrix_map_config = "config/matrix_map.conf";
    std::string board_profile = "skeleton_v4";
    std::string serial_device = "/dev/ttyACM0";
    uint32_t baud_rate = 115200;
    bool allow_missing_arduino = true;

    bool evdev_enabled = false;
    std::string evdev_device = "auto";
    std::string evdev_map_config = "config/evdev_map.conf";
    bool evdev_grab = true;
};

KeyboardConfig load_keyboard_config(const std::string &path);

} // namespace braillatron::keyboard
