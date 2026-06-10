#include "backend.h"

#include "../ui_config.h"
#include "../../telemetry/telemetry_config.h"
#include "../../telemetry/drv2605l.h"

#include <cstdlib>
#include <iostream>
#include <memory>

#ifdef BRAILLATRON_A11Y
#include <libspeechd.h>
#include <brlapi.h>
#endif

namespace braillatron::ui {

namespace {

class StubTtsBackend final : public TtsBackend {
public:
    bool available() const override { return false; }

    void speak(const std::string &text) override
    {
        std::cerr << "[tts] " << text << "\n";
    }

    void pause() override {}
    void resume() override {}
};

#ifdef BRAILLATRON_A11Y
class SpdTtsBackend final : public TtsBackend {
public:
    explicit SpdTtsBackend(std::string voice)
        : voice_(std::move(voice))
    {
        connection_ = spd_open("braillatron", "Braillatron UI", nullptr, SPD_MODE_THREADED);
        if (connection_ != nullptr && !voice_.empty()) {
            spd_set_synthesis_voice(connection_, voice_.c_str());
        }
    }

    ~SpdTtsBackend() override
    {
        if (connection_ != nullptr) {
            spd_close(connection_);
        }
    }

    bool available() const override { return connection_ != nullptr; }

    void speak(const std::string &text) override
    {
        if (connection_ == nullptr) {
            std::cerr << "[tts] " << text << "\n";
            return;
        }
        spd_say(connection_, SPD_MESSAGE, text.c_str());
    }

    void pause() override
    {
        if (connection_ != nullptr) {
            spd_pause(connection_);
        }
    }

    void resume() override
    {
        if (connection_ != nullptr) {
            spd_resume(connection_);
        }
    }

private:
    std::string voice_;
    SPDConnection *connection_ = nullptr;
};
#endif

class EspeakFallbackTtsBackend final : public TtsBackend {
public:
    bool available() const override { return true; }

    void speak(const std::string &text) override
    {
        const std::string cmd = "espeak-ng -s 150 \"" + text + "\" 2>/dev/null";
        std::system(cmd.c_str());
    }

    void pause() override {}
    void resume() override {}
};

class StubBrailleBackend final : public BrailleBackend {
public:
    bool available() const override { return false; }

    void write(const std::string &text) override
    {
        std::cerr << "[braille] " << text << "\n";
    }
};

#ifdef BRAILLATRON_A11Y
class BrlapiBackend final : public BrailleBackend {
public:
    BrlapiBackend()
    {
        if (brlapi_openConnection(nullptr, nullptr) >= 0) {
            open_ = true;
        }
    }

    ~BrlapiBackend() override
    {
        if (open_) {
            brlapi_closeConnection();
        }
    }

    bool available() const override { return open_; }

    void write(const std::string &text) override
    {
        if (!open_) {
            std::cerr << "[braille] " << text << "\n";
            return;
        }
        brlapi_writeText(BRLAPI_CURSOR_OFF, text.c_str());
    }

private:
    bool open_ = false;
};
#endif

class StubSttBackend final : public SttBackend {
public:
    bool available() const override { return false; }

    void set_ptt_open(bool open) override
    {
        if (open) {
            std::cerr << "[stt] push-to-talk open (stub)\n";
        }
    }
};

#ifdef BRAILLATRON_A11Y
class VoskSttBackend final : public SttBackend {
public:
    explicit VoskSttBackend(std::string model_path)
        : model_path_(std::move(model_path))
    {
        available_ = !model_path_.empty();
    }

    bool available() const override { return available_; }

    void set_ptt_open(bool open) override
    {
        ptt_open_ = open;
        if (open) {
            std::cerr << "[stt] vosk capture gate open\n";
        }
    }

private:
    std::string model_path_;
    bool available_ = false;
    bool ptt_open_ = false;
};
#endif

class Drv2605lHapticBackend final : public HapticBackend {
public:
    explicit Drv2605lHapticBackend(telemetry::TelemetryConfig config)
        : driver_(std::move(config))
    {
    }

    bool available() const override { return true; }

    void play_effect(uint8_t effect_id) override
    {
        (void)driver_.play_effect(effect_id);
    }

private:
    telemetry::Drv2605l driver_;
};

class StubHapticBackend final : public HapticBackend {
public:
    bool available() const override { return false; }

    void play_effect(uint8_t effect_id) override
    {
        std::cerr << "[haptics] effect " << static_cast<unsigned>(effect_id) << " (stub)\n";
    }
};

} // namespace

TtsBackend *create_tts_backend(const UiConfig &config)
{
#ifdef BRAILLATRON_A11Y
    if (config.tts_enabled) {
        auto *spd = new SpdTtsBackend(config.spd_voice);
        if (spd->available()) {
            return spd;
        }
        delete spd;
        return new EspeakFallbackTtsBackend();
    }
#endif
    (void)config;
    return new StubTtsBackend();
}

BrailleBackend *create_braille_backend(const UiConfig &config)
{
#ifdef BRAILLATRON_A11Y
    if (config.braille_enabled) {
        auto *brl = new BrlapiBackend();
        if (brl->available()) {
            return brl;
        }
        delete brl;
    }
#endif
    (void)config;
    return new StubBrailleBackend();
}

SttBackend *create_stt_backend(const UiConfig &config)
{
#ifdef BRAILLATRON_A11Y
    if (config.stt_enabled) {
        return new VoskSttBackend(config.vosk_model_path);
    }
#endif
    (void)config;
    return new StubSttBackend();
}

HapticBackend *create_haptic_backend(const UiConfig &ui_config,
                                     const telemetry::TelemetryConfig &telemetry_config)
{
    if (ui_config.haptics_enabled) {
        return new Drv2605lHapticBackend(telemetry_config);
    }
    (void)telemetry_config;
    return new StubHapticBackend();
}

} // namespace braillatron::ui
