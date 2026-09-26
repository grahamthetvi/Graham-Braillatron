#include "klipper_config.h"

#include <cctype>
#include <fstream>
#include <stdexcept>

namespace braillatron::motion {

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

KlipperConfig load_klipper_config(const std::string &path)
{
    KlipperConfig config;
    std::ifstream file(path);
    if (!file.is_open()) {
        return config;
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

        if (key == "enabled") {
            config.enabled = parse_bool(value);
        } else if (key == "moonraker_url") {
            config.moonraker_url = value;
        } else if (key == "request_timeout_sec") {
            config.request_timeout_sec = static_cast<uint32_t>(std::stoul(value));
        } else if (key == "y_feed_mm_per_line") {
            config.y_feed_mm_per_line = std::stod(value);
        } else if (key == "y_feed_speed_mm_s") {
            config.y_feed_speed_mm_s = std::stod(value);
        } else if (key == "x_move_speed_mm_s") {
            config.x_move_speed_mm_s = std::stod(value);
        } else if (key == "emboss_stroke_mm") {
            config.emboss_stroke_mm = std::stod(value);
        } else if (key == "emboss_speed_mm_s") {
            config.emboss_speed_mm_s = std::stod(value);
        }
    }

    return config;
}

} // namespace braillatron::motion
