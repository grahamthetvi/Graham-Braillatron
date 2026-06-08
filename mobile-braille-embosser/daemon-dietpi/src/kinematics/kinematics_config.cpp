#include "kinematics_config.h"

#include <cctype>
#include <fstream>
#include <stdexcept>

namespace braillatron::kinematics {

static std::string trim(const std::string &value)
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

KinematicsConfig load_kinematics_config(const std::string &path)
{
    KinematicsConfig config;
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("unable to open kinematics config: " + path);
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

        if (key == "microsteps_per_full_step") {
            config.microsteps_per_full_step = static_cast<uint32_t>(std::stoul(value));
        } else if (key == "nominal_carriage_speed_mm_s") {
            config.nominal_carriage_speed_mm_s = std::stod(value);
        } else if (key == "tdc_half_angle_deg") {
            config.tdc_half_angle_deg = std::stod(value);
        } else if (key == "crank_radius_mm") {
            config.crank_radius_mm = std::stod(value);
        } else if (key == "spatial_delay_line_capacity") {
            config.spatial_delay_line_capacity = static_cast<uint32_t>(std::stoul(value));
        }
    }

    return config;
}

} // namespace braillatron::kinematics
