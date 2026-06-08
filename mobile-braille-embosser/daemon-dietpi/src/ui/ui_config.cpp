#include "ui_config.h"

#include <cctype>
#include <fstream>
#include <stdexcept>

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

} // namespace

UiConfig load_ui_config(const std::string &path)
{
    UiConfig config;
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

        if (key == "tts_enabled") {
            config.tts_enabled = parse_bool(value);
        } else if (key == "braille_enabled") {
            config.braille_enabled = parse_bool(value);
        } else if (key == "stt_enabled") {
            config.stt_enabled = parse_bool(value);
        } else if (key == "haptics_enabled") {
            config.haptics_enabled = parse_bool(value);
        } else if (key == "spd_voice") {
            config.spd_voice = value;
        } else if (key == "vosk_model_path") {
            config.vosk_model_path = value;
        } else if (key == "boundary_haptic_effect") {
            config.boundary_haptic_effect = static_cast<uint8_t>(std::stoul(value));
        } else if (key == "status_probe_interval_ms") {
            config.status_probe_interval_ms = static_cast<uint32_t>(std::stoul(value));
        } else if (key == "heartbeat_interval_ms") {
            config.heartbeat_interval_ms = static_cast<uint32_t>(std::stoul(value));
        }
    }

    return config;
}

} // namespace braillatron::ui
