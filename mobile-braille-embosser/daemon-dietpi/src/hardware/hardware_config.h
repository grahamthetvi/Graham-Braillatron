#pragma once

#include <cstdint>
#include <string>

namespace braillatron::hardware {

struct HardwareConfig {
    std::string board_profile = "skeleton_v4";
    std::string arduino_device = "/dev/ttyACM0";
    uint32_t baud_rate = 115200;
    bool motion_enabled = false;
    bool allow_missing_arduino = true;
    std::string matrix_map_config = "config/matrix_map.conf";
    std::string telemetry_config = "config/telemetry.conf";
    std::string klipper_config = "config/klipper.conf";
};

HardwareConfig load_hardware_config(const std::string &path);

} // namespace braillatron::hardware
