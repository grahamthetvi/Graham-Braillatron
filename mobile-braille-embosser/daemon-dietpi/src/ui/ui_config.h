#pragma once

#include <cstdint>
#include <string>

namespace braillatron::ui {

struct UiConfig {
    bool tts_enabled = true;
    bool braille_enabled = true;
    bool stt_enabled = true;
    bool haptics_enabled = true;

    std::string spd_voice = "default";
    std::string vosk_model_path = "/data/braillatron/vosk-models/vosk-model-small-en-us-0.15";
    uint8_t boundary_haptic_effect = 3;
    uint32_t status_probe_interval_ms = 10000;
    uint32_t heartbeat_interval_ms = 1000;
};

UiConfig load_ui_config(const std::string &path);

} // namespace braillatron::ui
