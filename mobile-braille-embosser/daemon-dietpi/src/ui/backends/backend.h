#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace braillatron::documents {
class BrailleTranslationService;
}

namespace braillatron::telemetry {
struct TelemetryConfig;
}

namespace braillatron::motion {
class MotionService;
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
    virtual void set_rate(int rate) = 0;
    virtual void set_volume(int volume) = 0;
};

class BrailleBackend {
public:
    virtual ~BrailleBackend() = default;

    virtual bool available() const = 0;
    virtual void write(const std::string &text) = 0;
};

class SttBackend {
public:
    using TranscriptHandler = std::function<void(const std::string &text, bool is_final)>;

    virtual ~SttBackend() = default;

    virtual bool available() const = 0;
    virtual void set_ptt_open(bool open) = 0;
    virtual void set_transcript_handler(TranscriptHandler handler) = 0;
    virtual void preload() {}
};

class HapticBackend {
public:
    virtual ~HapticBackend() = default;

    virtual bool available() const = 0;
    virtual void play_effect(uint8_t effect_id) = 0;
};

class EmbosserBackend {
public:
    virtual ~EmbosserBackend() = default;

    virtual bool available() const = 0;
    virtual void enqueue_text(const std::string &plain) = 0;
    virtual void enqueue_dot_mask(uint8_t mask) = 0;
};

class MorseBackend {
public:
    virtual ~MorseBackend() = default;

    virtual bool available() const = 0;
    virtual void play_text(const std::string &text) = 0;
};

TtsBackend *create_tts_backend(const UiConfig &config);
BrailleBackend *create_braille_backend(const UiConfig &config,
                                       documents::BrailleTranslationService *braille);
SttBackend *create_stt_backend(const UiConfig &config);
HapticBackend *create_haptic_backend(const UiConfig &ui_config,
                                       const telemetry::TelemetryConfig &telemetry_config);
EmbosserBackend *create_embosser_backend(const UiConfig &config, motion::MotionService *motion,
                                           documents::BrailleTranslationService *braille);
MorseBackend *create_morse_backend(const UiConfig &config,
                                    const telemetry::TelemetryConfig &telemetry_config);

} // namespace braillatron::ui
