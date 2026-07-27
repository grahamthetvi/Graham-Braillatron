#include "backend.h"

#include "../../documents/liblouis_bridge.h"
#include "../../haptics/morse_encoder.h"
#include "../../motion/motion_service.h"
#include "../ui_config.h"
#include "../../telemetry/telemetry_config.h"
#include "../../telemetry/drv2605l.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

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
    void stop() override {}
    void set_rate(int) override {}
    void set_volume(int) override {}
};

#ifdef BRAILLATRON_A11Y
// All Speech Dispatcher I/O runs on a dedicated worker thread. spd_open (which
// can autospawn a daemon) and spd_say block the calling thread whenever the
// server is wedged (e.g. no usable audio sink). Routing every SSIP call through
// the worker guarantees the UI thread that drives navigation and keyboard input
// never stalls on TTS.
class SpdTtsBackend final : public TtsBackend {
public:
    explicit SpdTtsBackend(std::string voice)
        : voice_(std::move(voice))
    {
        worker_ = std::thread([this]() { worker_loop(); });
    }

    ~SpdTtsBackend() override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutting_down_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        if (connection_ != nullptr) {
            spd_close(connection_);
            connection_ = nullptr;
        }
    }

    // Never report unavailable from the UI thread: probing would require a
    // blocking connect. The worker logs to stderr if the daemon is unreachable.
    bool available() const override { return true; }

    // UI speech barges in: drop queued utterances, cancel whatever is playing,
    // then speak. Rapid menu / settings cycling must not build a backlog.
    void speak(const std::string &text) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutting_down_) {
                return;
            }
            drop_pending_speaks_locked();
            queue_.push_back(Command {CommandType::Stop, {}, 0});
            queue_.push_back(Command {CommandType::Speak, text, 0});
        }
        cv_.notify_all();
    }
    void pause() override { enqueue(Command {CommandType::Pause, {}, 0}); }
    void resume() override { enqueue(Command {CommandType::Resume, {}, 0}); }
    void stop() override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutting_down_) {
                return;
            }
            drop_pending_speaks_locked();
            queue_.push_back(Command {CommandType::Stop, {}, 0});
        }
        cv_.notify_all();
    }

    void set_rate(int rate) override
    {
        pending_rate_ = rate;
        enqueue(Command {CommandType::SetRate, {}, rate});
    }

    void set_volume(int volume) override
    {
        pending_volume_ = volume;
        enqueue(Command {CommandType::SetVolume, {}, volume});
    }

private:
    enum class CommandType { Speak, Pause, Resume, Stop, SetRate, SetVolume };

    struct Command {
        CommandType type;
        std::string text;
        int value;
    };

    void drop_pending_speaks_locked()
    {
        queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                    [](const Command &command) {
                                        return command.type == CommandType::Speak;
                                    }),
                     queue_.end());
    }

    void enqueue(Command command)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (shutting_down_) {
                return;
            }
            queue_.push_back(std::move(command));
        }
        cv_.notify_all();
    }

    void worker_loop()
    {
        for (;;) {
            Command command;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return shutting_down_ || !queue_.empty(); });
                if (queue_.empty()) {
                    return; // shutting_down_ with nothing left to flush.
                }
                command = std::move(queue_.front());
                queue_.pop_front();
            }
            process(command);
        }
    }

    void process(const Command &command)
    {
        switch (command.type) {
        case CommandType::Speak:
            if (connect()) {
                // Cancel in-flight speech so Speech Dispatcher does not queue behind it.
                spd_cancel(connection_);
                spd_say(connection_, SPD_MESSAGE, command.text.c_str());
            } else {
                std::cerr << "[tts] " << command.text << "\n";
            }
            break;
        case CommandType::Pause:
            if (connection_ != nullptr) {
                spd_pause(connection_);
            }
            break;
        case CommandType::Resume:
            if (connection_ != nullptr) {
                spd_resume(connection_);
            }
            break;
        case CommandType::Stop:
            if (connection_ != nullptr) {
                spd_cancel(connection_);
            }
            break;
        case CommandType::SetRate:
            if (connect()) {
                apply_rate(command.value);
            }
            break;
        case CommandType::SetVolume:
            if (connect()) {
                spd_set_volume(connection_, command.value);
            }
            break;
        }
    }

    // Runs on the worker thread only.
    bool connect()
    {
        if (connection_ != nullptr) {
            return true;
        }

        connection_ = spd_open("braillatron", "Braillatron UI", nullptr, SPD_MODE_SINGLE);
        if (connection_ == nullptr) {
            return false;
        }

        if (!voice_.empty()) {
            spd_set_synthesis_voice(connection_, voice_.c_str());
        }
        if (pending_rate_ >= 0) {
            apply_rate(pending_rate_);
        }
        if (pending_volume_ >= 0) {
            spd_set_volume(connection_, pending_volume_);
        }
        return true;
    }

    void apply_rate(int rate)
    {
        if (connection_ == nullptr) {
            return;
        }
        // ui.conf tts_rate is espeak WPM (~150 default); SPD voice rate is -100..100 (0 = normal).
        int spd_rate = (rate - 150) * 100 / 250;
        if (spd_rate < -100) {
            spd_rate = -100;
        } else if (spd_rate > 100) {
            spd_rate = 100;
        }
        spd_set_voice_rate(connection_, spd_rate);
    }

    std::string voice_;
    SPDConnection *connection_ = nullptr;
    int pending_rate_ = -1;
    int pending_volume_ = -1;

    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Command> queue_;
    bool shutting_down_ = false;
};
#endif

class EspeakFallbackTtsBackend final : public TtsBackend {
public:
    ~EspeakFallbackTtsBackend() override
    {
        stop();
    }

    bool available() const override { return true; }

    void speak(const std::string &text) override
    {
        stop();

        pid_t pid = fork();
        if (pid == 0) {
            // In child process: execute espeak-ng asynchronously
            execlp("espeak-ng", "espeak-ng", "-s", std::to_string(rate_).c_str(), text.c_str(), nullptr);
            _exit(127);
        } else if (pid > 0) {
            child_pid_ = pid;
        }
    }

    void pause() override {}
    void resume() override {}
    
    void stop() override
    {
        if (child_pid_ > 0) {
            kill(child_pid_, SIGKILL);
            int status;
            waitpid(child_pid_, &status, 0);
            child_pid_ = -1;
        }
    }

    void set_rate(int rate) override { rate_ = rate; }
    void set_volume(int) override {}

private:
    int rate_ = 150;
    pid_t child_pid_ = -1;
};

class StubBrailleBackend final : public BrailleBackend {
public:
    bool available() const override { return false; }
    void write(const std::string &text) override { std::cerr << "[braille] " << text << "\n"; }
};

#ifdef BRAILLATRON_A11Y
class BrlapiBackend final : public BrailleBackend {
public:
    explicit BrlapiBackend(documents::BrailleTranslationService *braille)
        : braille_(braille)
    {
    }

    ~BrlapiBackend() override
    {
        if (open_) {
            brlapi_closeConnection();
            open_ = false;
        }
    }

    bool available() const override
    {
        return ensure_open();
    }

    void write(const std::string &text) override
    {
        if (!ensure_open()) {
            std::cerr << "[braille] " << text << "\n";
            return;
        }
        const std::string translated =
            braille_ != nullptr ? braille_->translate_forward(text) : text;
        brlapi_writeText(BRLAPI_CURSOR_OFF, translated.c_str());
    }

private:
    bool ensure_open() const
    {
        if (open_) {
            return true;
        }
        if (brlapi_openConnection(nullptr, nullptr) >= 0) {
            open_ = true;
        }
        return open_;
    }

    mutable bool open_ = false;
    documents::BrailleTranslationService *braille_ = nullptr;
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

    ~VoskSttBackend() override
    {
        stop_capture();
        if (preload_thread_.joinable()) {
            preload_thread_.join();
        }
        std::lock_guard<std::mutex> lock(model_mutex_);
        if (cached_model_ != nullptr) {
            vosk_model_free(cached_model_);
            cached_model_ = nullptr;
        }
    }

    bool available() const override { return available_; }

    void set_transcript_handler(TranscriptHandler handler) override
    {
        handler_ = std::move(handler);
    }

    void preload() override
    {
        if (!available_ || preload_started_.exchange(true)) {
            return;
        }
        preload_thread_ = std::thread([this]() {
            std::lock_guard<std::mutex> lock(model_mutex_);
            if (cached_model_ != nullptr) {
                return;
            }
            cached_model_ = vosk_model_new(model_path_.c_str());
            if (cached_model_ != nullptr) {
                std::cerr << "[stt] vosk model preloaded\n";
            } else {
                std::cerr << "[stt] vosk preload failed\n";
            }
        });
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
    VoskModel *acquire_model()
    {
        std::lock_guard<std::mutex> lock(model_mutex_);
        if (cached_model_ != nullptr) {
            VoskModel *model = cached_model_;
            cached_model_ = nullptr;
            return model;
        }
        return vosk_model_new(model_path_.c_str());
    }

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
        VoskModel *model = acquire_model();
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
    std::atomic<bool> preload_started_ {false};
    std::thread capture_thread_;
    std::thread preload_thread_;
    std::mutex model_mutex_;
    VoskModel *cached_model_ = nullptr;
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
    MotionEmbosserBackend(motion::MotionService *motion,
                          documents::BrailleTranslationService *braille)
        : motion_(motion)
        , braille_(braille)
    {
    }

    bool available() const override { return motion_ != nullptr; }

    void enqueue_text(const std::string &plain) override
    {
        if (motion_ != nullptr && braille_ != nullptr) {
            motion_->emboss_text(plain, *braille_);
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
    documents::BrailleTranslationService *braille_ = nullptr;
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
        spd->set_rate(config.tts_rate);
        spd->set_volume(config.tts_volume);
        return spd;
    }
#endif
    (void)config;
    return new StubTtsBackend();
}

BrailleBackend *create_braille_backend(const UiConfig &config,
                                       documents::BrailleTranslationService *braille)
{
#ifdef BRAILLATRON_A11Y
    if (config.braille_enabled) {
        return new BrlapiBackend(braille);
    }
#endif
    (void)config;
    (void)braille;
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

EmbosserBackend *create_embosser_backend(const UiConfig &config, motion::MotionService *motion,
                                         documents::BrailleTranslationService *braille)
{
    if (config.embosser_enabled && motion != nullptr) {
        return new MotionEmbosserBackend(motion, braille);
    }
    (void)braille;
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
