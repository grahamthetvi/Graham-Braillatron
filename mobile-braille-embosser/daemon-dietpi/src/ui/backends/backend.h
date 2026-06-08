#pragma once

#include <cstdint>
#include <string>

namespace braillatron::telemetry {
struct TelemetryConfig;
}

namespace braillatron::ui {

struct UiConfig;

class TtsBackend {
public:
    virtual ~TtsBackend() = default;

    virtual bool available() const = 0;
    virtual void speak(const std::string &text) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
};

class BrailleBackend {
public:
    virtual ~BrailleBackend() = default;

    virtual bool available() const = 0;
    virtual void write(const std::string &text) = 0;
};

class SttBackend {
public:
    virtual ~SttBackend() = default;

    virtual bool available() const = 0;
    virtual void set_ptt_open(bool open) = 0;
};

class HapticBackend {
public:
    virtual ~HapticBackend() = default;

    virtual bool available() const = 0;
    virtual void play_effect(uint8_t effect_id) = 0;
};

TtsBackend *create_tts_backend(const UiConfig &config);
BrailleBackend *create_braille_backend(const UiConfig &config);
SttBackend *create_stt_backend(const UiConfig &config);
HapticBackend *create_haptic_backend(const UiConfig &ui_config,
                                       const telemetry::TelemetryConfig &telemetry_config);

} // namespace braillatron::ui
