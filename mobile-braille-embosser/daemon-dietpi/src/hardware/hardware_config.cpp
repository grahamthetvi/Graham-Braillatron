#include "hardware_config.h"

#include <cctype>
#include <fstream>
#include <stdexcept>

namespace braillatron::hardware {

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

bool parse_bool(const std::string &value)
{
    const std::string lower = trim(value);
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
}

} // namespace

HardwareConfig load_hardware_config(const std::string &path)
{
    HardwareConfig config;
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("unable to open hardware config: " + path);
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

        if (key == "board_profile") {
            config.board_profile = value;
        } else if (key == "arduino_device") {
            config.arduino_device = value;
        } else if (key == "baud_rate") {
            config.baud_rate = static_cast<uint32_t>(std::stoul(value));
        } else if (key == "motion_enabled") {
            config.motion_enabled = parse_bool(value);
        } else if (key == "allow_missing_arduino") {
            config.allow_missing_arduino = parse_bool(value);
        } else if (key == "matrix_map_config") {
            config.matrix_map_config = value;
        } else if (key == "telemetry_config") {
            config.telemetry_config = value;
        } else if (key == "klipper_config") {
            config.klipper_config = value;
        }
    }

    return config;
}

} // namespace braillatron::hardware
