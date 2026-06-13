#pragma once

#include <cstdint>
#include <string>

namespace braillatron::ui {

struct UiConfig {
    bool tts_enabled = true;
    bool braille_enabled = true;
    bool stt_enabled = true;
    bool haptics_enabled = true;
    bool embosser_enabled = true;
    bool deaf_blind_menu_parity = true;
    bool morse_output_enabled = false;
    bool display_enabled = true;

    std::string spd_voice = "default";
    std::string vosk_model_path = "/data/braillatron/vosk-models/vosk-model-small-en-us-0.15";
    std::string braille_table = "ueb_g2_math";
    std::string language = "en-US";

    int tts_rate = 150;
    int tts_volume = 100;
    uint8_t haptic_intensity = 3;
    uint8_t boundary_haptic_effect = 3;
    uint32_t morse_wpm = 12;

    uint32_t status_probe_interval_ms = 10000;
    uint32_t heartbeat_interval_ms = 1000;
    uint32_t transcriber_queue_limit = 8;
};

UiConfig load_ui_config(const std::string &path);
void save_ui_config(const std::string &path, const UiConfig &config);

} // namespace braillatron::ui
