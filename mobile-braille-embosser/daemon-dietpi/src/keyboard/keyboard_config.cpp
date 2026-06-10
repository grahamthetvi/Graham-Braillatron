#include "keyboard_config.h"

#include "../hardware/hardware_config.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace braillatron::keyboard {

namespace {

std::string trim(const std::string &value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(start, end - start);
}

std::string resolve_config_path(const std::string &path)
{
    if (path.empty() || path[0] == '/') {
        return path;
    }

    const char *env = std::getenv("BRAILLATRON_CONFIG");
    const std::string base = (env != nullptr && env[0] != '\0') ? env : "config";
    return base + "/" + path;
}

} // namespace

KeyboardConfig load_keyboard_config(const std::string &path)
{
    KeyboardConfig config;
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("unable to open keyboard config: " + path);
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));

        if (key == "hardware_config") {
            config.hardware_config_path = value;
        } else if (key == "matrix_map_config") {
            config.matrix_map_config = value;
        } else if (key == "serial_device") {
            config.serial_device = value;
        } else if (key == "baud_rate") {
            config.baud_rate = static_cast<uint32_t>(std::stoul(value));
        } else if (key == "evdev_enabled") {
            config.evdev_enabled = (value == "1" || value == "true" || value == "yes");
        } else if (key == "evdev_device") {
            config.evdev_device = value;
        } else if (key == "evdev_map_config") {
            config.evdev_map_config = value;
        } else if (key == "evdev_grab") {
            config.evdev_grab = (value == "1" || value == "true" || value == "yes");
        }
    }

    if (!config.hardware_config_path.empty()) {
        const braillatron::hardware::HardwareConfig hardware =
            braillatron::hardware::load_hardware_config(
                resolve_config_path(config.hardware_config_path));
        config.board_profile = hardware.board_profile;
        config.serial_device = hardware.arduino_device;
        config.baud_rate = hardware.baud_rate;
        config.matrix_map_config = hardware.matrix_map_config;
        config.allow_missing_arduino = hardware.allow_missing_arduino;
    }

    return config;
}

} // namespace braillatron::keyboard
