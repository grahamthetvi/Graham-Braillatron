#include "ui_config.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
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
        } else if (key == "embosser_enabled") {
            config.embosser_enabled = parse_bool(value);
        } else if (key == "deaf_blind_menu_parity") {
            config.deaf_blind_menu_parity = parse_bool(value);
        } else if (key == "morse_output_enabled") {
            config.morse_output_enabled = parse_bool(value);
        } else if (key == "display_enabled") {
            config.display_enabled = parse_bool(value);
        } else if (key == "document_dictation_enabled") {
            config.document_dictation_enabled = parse_bool(value);
        } else if (key == "spd_voice") {
            config.spd_voice = value;
        } else if (key == "vosk_model_path") {
            config.vosk_model_path = value;
        } else if (key == "braille_table") {
            config.braille_table = value;
        } else if (key == "language") {
            config.language = value;
        } else if (key == "tts_rate") {
            config.tts_rate = std::stoi(value);
        } else if (key == "tts_volume") {
            config.tts_volume = std::stoi(value);
        } else if (key == "haptic_intensity") {
            config.haptic_intensity = static_cast<uint8_t>(std::stoul(value));
        } else if (key == "boundary_haptic_effect") {
            config.boundary_haptic_effect = static_cast<uint8_t>(std::stoul(value));
        } else if (key == "morse_wpm") {
            config.morse_wpm = static_cast<uint32_t>(std::stoul(value));
        } else if (key == "status_probe_interval_ms") {
            config.status_probe_interval_ms = static_cast<uint32_t>(std::stoul(value));
        } else if (key == "heartbeat_interval_ms") {
            config.heartbeat_interval_ms = static_cast<uint32_t>(std::stoul(value));
        } else if (key == "transcriber_queue_limit") {
            config.transcriber_queue_limit = static_cast<uint32_t>(std::stoul(value));
        }
    }

    return config;
}

void save_ui_config(const std::string &path, const UiConfig &config)
{
    std::ostringstream stream;
    stream << "# UI output hub — accessibility backends and probe timing\n";
    stream << "tts_enabled=" << (config.tts_enabled ? "true" : "false") << "\n";
    stream << "braille_enabled=" << (config.braille_enabled ? "true" : "false") << "\n";
    stream << "stt_enabled=" << (config.stt_enabled ? "true" : "false") << "\n";
    stream << "haptics_enabled=" << (config.haptics_enabled ? "true" : "false") << "\n";
    stream << "embosser_enabled=" << (config.embosser_enabled ? "true" : "false") << "\n";
    stream << "deaf_blind_menu_parity=" << (config.deaf_blind_menu_parity ? "true" : "false")
           << "\n";
    stream << "morse_output_enabled=" << (config.morse_output_enabled ? "true" : "false") << "\n";
    stream << "display_enabled=" << (config.display_enabled ? "true" : "false") << "\n";
    stream << "document_dictation_enabled="
           << (config.document_dictation_enabled ? "true" : "false") << "\n";
    stream << "\n";
    stream << "spd_voice=" << config.spd_voice << "\n";
    stream << "vosk_model_path=" << config.vosk_model_path << "\n";
    stream << "braille_table=" << config.braille_table << "\n";
    stream << "language=" << config.language << "\n";
    stream << "tts_rate=" << config.tts_rate << "\n";
    stream << "tts_volume=" << config.tts_volume << "\n";
    stream << "haptic_intensity=" << static_cast<unsigned>(config.haptic_intensity) << "\n";
    stream << "boundary_haptic_effect=" << static_cast<unsigned>(config.boundary_haptic_effect)
           << "\n";
    stream << "morse_wpm=" << config.morse_wpm << "\n";
    stream << "status_probe_interval_ms=" << config.status_probe_interval_ms << "\n";
    stream << "heartbeat_interval_ms=" << config.heartbeat_interval_ms << "\n";
    stream << "transcriber_queue_limit=" << config.transcriber_queue_limit << "\n";

    const std::string tmp_path = path + ".tmp";
    {
        std::ofstream file(tmp_path, std::ios::trunc);
        if (!file.is_open()) {
            throw std::runtime_error("failed to open ui config for writing: " + tmp_path);
        }
        file << stream.str();
        file.flush();
        if (!file.good()) {
            throw std::runtime_error("failed to write ui config: " + tmp_path);
        }
    }

    if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
        throw std::runtime_error("failed to rename ui config: " + tmp_path + " -> " + path);
    }
}

} // namespace braillatron::ui
