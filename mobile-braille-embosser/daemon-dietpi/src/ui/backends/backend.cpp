#include "backend.h"

#include "../../documents/liblouis_bridge.h"
#include "../../haptics/morse_encoder.h"
#include "../../motion/motion_service.h"
#include "../ui_config.h"
#include "../../telemetry/telemetry_config.h"
#include "../../telemetry/drv2605l.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <memory>
#include <thread>

#ifdef BRAILLATRON_A11Y
#include <libspeechd.h>
#include <brlapi.h>
#include <vosk_api.h>
#endif

namespace braillatron::ui {

namespace {

class StubTtsBackend final : public TtsBackend {
public:
    bool available() const override { return false; }
    void speak(const std::string &text) override { std::cerr << "[tts] " << text << "\n"; }
    void pause() override {}
    void resume() override {}
    void set_rate(int) override {}
    void set_volume(int) override {}
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

    void set_rate(int rate) override
    {
        if (connection_ != nullptr) {
            spd_set_rate(connection_, rate);
        }
    }

    void set_volume(int volume) override
    {
        if (connection_ != nullptr) {
            spd_set_volume(connection_, volume);
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
        const std::string cmd =
            "espeak-ng -s " + std::to_string(rate_) + " \"" + text + "\" 2>/dev/null";
        std::system(cmd.c_str());
    }

    void pause() override {}
    void resume() override {}
    void set_rate(int rate) override { rate_ = rate; }
    void set_volume(int) override {}

private:
    int rate_ = 150;
};

class StubBrailleBackend final : public BrailleBackend {
public:
    bool available() const override { return false; }
    void write(const std::string &text) override { std::cerr << "[braille] " << text << "\n"; }
};

#ifdef BRAILLATRON_A11Y
class BrlapiBackend final : public BrailleBackend {
public:
    explicit BrlapiBackend(documents::BrailleTable table)
        : table_(table)
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
        const std::string translated = documents::translate_forward(text, table_);
        brlapi_writeText(BRLAPI_CURSOR_OFF, translated.c_str());
    }

    void set_table(documents::BrailleTable table) { table_ = table; }

private:
    bool open_ = false;
    documents::BrailleTable table_;
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
    void set_transcript_handler(TranscriptHandler) override {}
};

#ifdef BRAILLATRON_A11Y
class VoskSttBackend final : public SttBackend {
public:
    explicit VoskSttBackend(std::string model_path)
        : model_path_(std::move(model_path))
    {
        available_ = !model_path_.empty();
    }

    ~VoskSttBackend() override { stop_capture(); }

    bool available() const override { return available_; }

    void set_transcript_handler(TranscriptHandler handler) override
    {
        handler_ = std::move(handler);
    }

    void set_ptt_open(bool open) override
    {
        if (open == ptt_open_) {
            return;
        }
        ptt_open_ = open;
        if (open) {
            start_capture();
        } else {
            stop_capture();
        }
    }

private:
    void start_capture()
    {
        if (capture_running_.load()) {
            return;
        }
        capture_running_ = true;
        capture_thread_ = std::thread([this]() { capture_loop(); });
    }

    void stop_capture()
    {
        capture_running_ = false;
        if (capture_thread_.joinable()) {
            capture_thread_.join();
        }
    }

    void capture_loop()
    {
        VoskModel *model = vosk_model_new(model_path_.c_str());
        if (model == nullptr) {
            std::cerr << "[stt] failed to load vosk model\n";
            capture_running_ = false;
            return;
        }

        VoskRecognizer *recognizer = vosk_recognizer_new(model, 16000.0f);
        FILE *pipe = popen("arecord -q -f S16_LE -r 16000 -c 1 2>/dev/null", "r");
        if (pipe == nullptr) {
            vosk_recognizer_free(recognizer);
            vosk_model_free(model);
            capture_running_ = false;
            return;
        }

        std::cerr << "[stt] vosk capture started\n";
        char buffer[4096];
        while (capture_running_.load()) {
            const size_t n = fread(buffer, 1, sizeof(buffer), pipe);
            if (n == 0) {
                break;
            }
            if (vosk_recognizer_accept_waveform(recognizer, buffer, static_cast<int>(n))) {
                emit_transcript(vosk_recognizer_result(recognizer), true);
            } else {
                emit_transcript(vosk_recognizer_partial_result(recognizer), false);
            }
        }

        emit_transcript(vosk_recognizer_final_result(recognizer), true);
        pclose(pipe);
        vosk_recognizer_free(recognizer);
        vosk_model_free(model);
        std::cerr << "[stt] vosk capture stopped\n";
    }

    void emit_transcript(const char *json, bool is_final)
    {
        if (handler_ == nullptr || json == nullptr) {
            return;
        }
        const std::string payload(json);
        const std::string key = is_final ? "\"text\" : \"" : "\"partial\" : \"";
        const size_t pos = payload.find(key);
        if (pos == std::string::npos) {
            return;
        }
        const size_t start = pos + key.size();
        const size_t end = payload.find('"', start);
        if (end == std::string::npos) {
            return;
        }
        handler_(payload.substr(start, end - start), is_final);
    }

    std::string model_path_;
    bool available_ = false;
    bool ptt_open_ = false;
    TranscriptHandler handler_;
    std::atomic<bool> capture_running_ {false};
    std::thread capture_thread_;
};
#endif

class Drv2605lHapticBackend final : public HapticBackend {
public:
    explicit Drv2605lHapticBackend(telemetry::TelemetryConfig config, uint8_t intensity)
        : driver_(std::move(config))
        , intensity_(intensity)
    {
    }

    bool available() const override { return true; }

    void play_effect(uint8_t effect_id) override
    {
        (void)driver_.play_effect(effect_id != 0 ? effect_id : intensity_);
    }

private:
    telemetry::Drv2605l driver_;
    uint8_t intensity_;
};

class StubHapticBackend final : public HapticBackend {
public:
    bool available() const override { return false; }
    void play_effect(uint8_t effect_id) override
    {
        std::cerr << "[haptics] effect " << static_cast<unsigned>(effect_id) << " (stub)\n";
    }
};

class MotionEmbosserBackend final : public EmbosserBackend {
public:
    MotionEmbosserBackend(motion::MotionService *motion, documents::BrailleTable table)
        : motion_(motion)
        , table_(table)
    {
    }

    bool available() const override { return motion_ != nullptr; }

    void enqueue_text(const std::string &plain) override
    {
        if (motion_ != nullptr) {
            motion_->emboss_text(plain, table_);
        }
    }

    void enqueue_dot_mask(uint8_t mask) override
    {
        if (motion_ != nullptr) {
            motion_->emboss_dot_mask(mask);
        }
    }

private:
    motion::MotionService *motion_ = nullptr;
    documents::BrailleTable table_;
};

class StubEmbosserBackend final : public EmbosserBackend {
public:
    bool available() const override { return false; }
    void enqueue_text(const std::string &text) override
    {
        std::cerr << "[embosser] " << text << "\n";
    }
    void enqueue_dot_mask(uint8_t mask) override
    {
        std::cerr << "[embosser] dot mask 0x" << std::hex << static_cast<unsigned>(mask)
                  << std::dec << "\n";
    }
};

class MorseHapticBackend final : public MorseBackend {
public:
    MorseHapticBackend(telemetry::TelemetryConfig config, uint32_t wpm)
        : driver_(std::move(config))
    {
        encoder_.set_wpm(wpm);
        encoder_.set_pulse_handler([this](uint32_t duration_ms) {
            (void)driver_.play_effect(3);
            std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
        });
    }

    bool available() const override { return true; }

    void play_text(const std::string &text) override
    {
        std::thread([this, text]() { encoder_.play_text(text); }).detach();
    }

private:
    telemetry::Drv2605l driver_;
    haptics::MorseEncoder encoder_;
};

class StubMorseBackend final : public MorseBackend {
public:
    bool available() const override { return false; }
    void play_text(const std::string &text) override
    {
        std::cerr << "[morse] " << text << "\n";
    }
};

} // namespace

TtsBackend *create_tts_backend(const UiConfig &config)
{
#ifdef BRAILLATRON_A11Y
    if (config.tts_enabled) {
        auto *spd = new SpdTtsBackend(config.spd_voice);
        if (spd->available()) {
            spd->set_rate(config.tts_rate);
            spd->set_volume(config.tts_volume);
            return spd;
        }
        delete spd;
        auto *espeak = new EspeakFallbackTtsBackend();
        espeak->set_rate(config.tts_rate);
        return espeak;
    }
#endif
    (void)config;
    return new StubTtsBackend();
}

BrailleBackend *create_braille_backend(const UiConfig &config)
{
#ifdef BRAILLATRON_A11Y
    if (config.braille_enabled) {
        auto *brl = new BrlapiBackend(documents::braille_table_from_string(config.braille_table));
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
        return new Drv2605lHapticBackend(telemetry_config, ui_config.haptic_intensity);
    }
    (void)telemetry_config;
    return new StubHapticBackend();
}

EmbosserBackend *create_embosser_backend(const UiConfig &config, motion::MotionService *motion)
{
    if (config.embosser_enabled && motion != nullptr) {
        return new MotionEmbosserBackend(motion,
                                         documents::braille_table_from_string(config.braille_table));
    }
    (void)motion;
    return new StubEmbosserBackend();
}

MorseBackend *create_morse_backend(const UiConfig &config,
                                    const telemetry::TelemetryConfig &telemetry_config)
{
    if (config.morse_output_enabled || config.haptics_enabled) {
        return new MorseHapticBackend(telemetry_config, config.morse_wpm);
    }
    (void)telemetry_config;
    return new StubMorseBackend();
}

} // namespace braillatron::ui
