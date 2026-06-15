#include "display_config.h"

#include <cctype>
#include <fstream>

namespace braillatron::ui {

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

DisplayBackendKind parse_backend_kind(const std::string &value)
{
    const std::string lower = trim(value);
    if (lower == "spi") {
        return DisplayBackendKind::Spi;
    }
    if (lower == "ncurses") {
        return DisplayBackendKind::Ncurses;
    }
    if (lower == "fb" || lower == "fbdev" || lower == "hdmi") {
        return DisplayBackendKind::Fb;
    }
    if (lower == "multi" || lower == "composite") {
        return DisplayBackendKind::Multi;
    }
    if (lower == "stub") {
        return DisplayBackendKind::Stub;
    }
    return DisplayBackendKind::Auto;
}

} // namespace

DisplayConfig load_display_config(const std::string &path)
{
    DisplayConfig config;
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

        if (key == "backend") {
            config.backend = parse_backend_kind(value);
        } else if (key == "spidev") {
            config.spidev = value;
        } else if (key == "width") {
            config.width = static_cast<uint16_t>(std::stoul(value));
        } else if (key == "height") {
            config.height = static_cast<uint16_t>(std::stoul(value));
        } else if (key == "gpio_dc") {
            config.gpio_dc = std::stoi(value);
        } else if (key == "gpio_rst") {
            config.gpio_rst = std::stoi(value);
        } else if (key == "gpio_cs") {
            config.gpio_cs = std::stoi(value);
        } else if (key == "ncurses_enabled") {
            config.ncurses_enabled = parse_bool(value);
        } else if (key == "fbdev") {
            config.fbdev = value;
        } else if (key == "hdmi_enabled") {
            config.hdmi_enabled = parse_bool(value);
        } else if (key == "hdmi_font_scale") {
            config.hdmi_font_scale = std::stoi(value);
        } else if (key == "remote_display_enabled") {
            config.remote_display_enabled = parse_bool(value);
        } else if (key == "remote_frame_socket") {
            config.remote_frame_socket = value;
        } else if (key == "remote_cmd_socket") {
            config.remote_cmd_socket = value;
        }
    }

    return config;
}

} // namespace braillatron::ui
