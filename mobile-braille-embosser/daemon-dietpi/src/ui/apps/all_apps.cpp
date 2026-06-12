#include "app_registry.h"
#include "app_session.h"
#include "ui_context.h"

#include "../output_hub.h"

#include "../../documents/edit_session.h"
#include "../../motion/motion_service.h"
#include "../../platform/shell_util.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <memory>
#include <regex>
#include <sstream>
#include <string>

namespace braillatron::ui {
namespace {

void announce(UiContext &ctx, const std::string &msg)
{
    if (ctx.output != nullptr) {
        ctx.output->announce_message(msg);
    }
}

class BraillerApp final : public AppSession {
public:
    std::string id() const override { return "brailler"; }
    std::string label() const override { return "Document"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        if (ctx.brf != nullptr) {
            ctx.brf->load();
        }
        if (ctx.coords != nullptr) {
            ctx.coords->mutable_state().active_app_id = id();
            ctx.coords->save();
        }
        if (ctx.edit != nullptr) {
            ctx.edit->set_brf_store(ctx.brf);
            ctx.edit->set_announce([&ctx](const std::string &m) { announce(ctx, m); });
            if (ctx.motion != nullptr) {
                ctx.edit->set_advance_line([&ctx]() {
                    if (ctx.motion != nullptr) {
                        ctx.motion->advance_line();
                    }
                });
            }
        }
        announce(ctx, "Brailler ready");
    }

    void on_exit(UiContext &ctx) override
    {
        if (ctx.brf != nullptr) {
            ctx.brf->save();
        }
        if (ctx.coords != nullptr) {
            ctx.coords->mutable_state().active_app_id.clear();
            ctx.coords->save();
        }
        announce(ctx, "Brailler closed");
    }

    void on_poll(UiContext &) override {}

    void on_chord(uint8_t dot_mask, UiContext &ctx) override
    {
        if (ctx.edit != nullptr) {
            ctx.edit->on_full_cell(dot_mask);
        }
    }

    void on_text(const std::string &text, UiContext &ctx) override
    {
        if (ctx.brf == nullptr) {
            return;
        }
        for (char ch : text) {
            ctx.brf->append_char(ch);
        }
        ctx.brf->save();

        static const std::regex worksheet_pattern(R"((Name|Date)\s*:)",
                                                  std::regex::icase);
        if (std::regex_search(ctx.brf->full_text(), worksheet_pattern)) {
            announce(ctx, "Worksheet session recording");
        }

        if (ctx.edit != nullptr && ctx.edit->state() == documents::EditState::ReplacementLine) {
            ctx.edit->on_replacement_chord(0, text);
        }
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed || key != keyboard::ControlKey::Enter) {
            return;
        }
        if (ctx.edit != nullptr && ctx.brf != nullptr) {
            ctx.edit->begin_line_review(ctx.brf->line_count() > 0 ? ctx.brf->line_count() - 1 : 0);
        }
    }
};

enum class CalcAudioMode { Char, Silent, SpaceAffirm };

class CalculatorApp final : public AppSession {
public:
    std::string id() const override { return "calculator"; }
    std::string label() const override { return "Calculator"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        buffer_.clear();
        announce(ctx, "Calculator ready.");
    }

    void on_exit(UiContext &ctx) override { announce(ctx, "Calculator closed"); }
    void on_poll(UiContext &) override {}

    void on_chord(uint8_t, UiContext &) override {}

    void on_text(const std::string &text, UiContext &ctx) override
    {
        for (char ch : text) {
            if (ch == ' ') {
                if (mode_ != CalcAudioMode::Silent) {
                    announce(ctx, "Equation: " + buffer_);
                }
                if (ctx.motion != nullptr && ctx.braille != nullptr &&
                    mode_ == CalcAudioMode::SpaceAffirm) {
                    ctx.motion->emboss_text(buffer_, *ctx.braille);
                }
                buffer_.push_back(ch);
                continue;
            }
            buffer_.push_back(ch);
            if (mode_ == CalcAudioMode::Char) {
                announce(ctx, std::string(1, ch));
            }
        }
    }

    void on_control(keyboard::ControlKey, bool, UiContext &) override {}

private:
    std::string buffer_;
    CalcAudioMode mode_ = CalcAudioMode::Char;
};

class TranscriberApp final : public AppSession {
public:
    std::string id() const override { return "transcriber"; }
    std::string label() const override { return "Transcriber"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        queue_depth_ = 0;
        motion_ = ctx.motion;
        brf_ = ctx.brf;
        output_ = ctx.output;
        braille_ = ctx.braille;
        if (output_ != nullptr) {
            output_->set_stt_transcript_handler(
                [this](const std::string &text, bool is_final) {
                    if (!is_final || text.empty()) {
                        return;
                    }
                    ++queue_depth_;
                    if (queue_depth_ > 8) {
                        if (output_ != nullptr) {
                            output_->announce_message("Buffer full. Please pause.");
                            output_->play_boundary_haptic();
                        }
                        return;
                    }
                    if (motion_ != nullptr && braille_ != nullptr) {
                        motion_->emboss_text(text, *braille_);
                    }
                    if (brf_ != nullptr) {
                        brf_->append_line(text);
                        brf_->save();
                    }
                    --queue_depth_;
                });
        }
        announce(ctx, "Transcriber listening. Hold speech button.");
    }

    void on_exit(UiContext &ctx) override
    {
        if (output_ != nullptr) {
            output_->set_stt_transcript_handler(nullptr);
        }
        motion_ = nullptr;
        brf_ = nullptr;
        output_ = nullptr;
        braille_ = nullptr;
        announce(ctx, "Transcriber closed");
    }

    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}
    void on_text(const std::string &, UiContext &) override {}
    void on_control(keyboard::ControlKey, bool, UiContext &) override {}

private:
    uint32_t queue_depth_ = 0;
    motion::MotionService *motion_ = nullptr;
    documents::BrfStore *brf_ = nullptr;
    documents::BrailleTranslationService *braille_ = nullptr;
    OutputHub *output_ = nullptr;
};

class MorseLearnApp final : public AppSession {
public:
    std::string id() const override { return "morse_learn"; }
    std::string label() const override { return "Morse Learning"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        lesson_index_ = 0;
        announce(ctx, "Morse lesson. Letter " + std::string(1, kAlphabet[lesson_index_]));
        if (ctx.output != nullptr) {
            ctx.output->play_morse(std::string(1, kAlphabet[lesson_index_]));
        }
    }

    void on_exit(UiContext &ctx) override { announce(ctx, "Morse lesson closed"); }
    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}

    void on_text(const std::string &text, UiContext &ctx) override
    {
        if (text.empty()) {
            return;
        }
        const char expected = kAlphabet[lesson_index_];
        if (text[0] == expected || text[0] == static_cast<char>(std::toupper(text[0]))) {
            announce(ctx, "Correct");
            lesson_index_ = (lesson_index_ + 1) % (sizeof(kAlphabet) - 1);
            announce(ctx, "Next: " + std::string(1, kAlphabet[lesson_index_]));
            if (ctx.output != nullptr) {
                ctx.output->play_morse(std::string(1, kAlphabet[lesson_index_]));
            }
        } else {
            announce(ctx, "Try again");
        }
    }

    void on_control(keyboard::ControlKey, bool, UiContext &) override {}

private:
    static constexpr const char *kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    size_t lesson_index_ = 0;
};

class NetworkApp final : public AppSession {
public:
    std::string id() const override { return "network"; }
    std::string label() const override { return "Network and Devices"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        announce(ctx, "Scanning Wi-Fi networks");
        const std::string scan =
            platform::run_command("nmcli -t -f SSID dev wifi list 2>/dev/null");
        std::istringstream stream(scan);
        std::string line;
        int count = 0;
        while (std::getline(stream, line) && count < 10) {
            if (line.empty()) {
                continue;
            }
            announce(ctx, line);
            ++count;
        }
        const std::string bt =
            platform::run_command("nmcli -t -f NAME,TYPE dev status 2>/dev/null | grep bluetooth");
        if (!bt.empty()) {
            announce(ctx, "Bluetooth devices listed in log");
        }
        announce(ctx, "Use QWERTY to enter password after selecting SSID");
    }

    void on_exit(UiContext &ctx) override { announce(ctx, "Network closed"); }
    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}

    void on_text(const std::string &text, UiContext &ctx) override
    {
        if (text.empty()) {
            return;
        }
        if (pending_ssid_.empty()) {
            pending_ssid_ = text;
            announce(ctx, "Selected " + pending_ssid_ + ". Enter password.");
            return;
        }
        pending_password_ += text;
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed || key != keyboard::ControlKey::Enter || pending_ssid_.empty() ||
            pending_password_.empty()) {
            return;
        }
        const std::string cmd = "nmcli dev wifi connect \"" + pending_ssid_ + "\" password \"" +
                                pending_password_ + "\" 2>&1";
        const std::string result = platform::run_command(cmd);
        announce(ctx, result.empty() ? "Connection attempted" : result);
    }

private:
    std::string pending_ssid_;
    std::string pending_password_;
};

class LibraryApp final : public AppSession {
public:
    std::string id() const override { return "library"; }
    std::string label() const override { return "Library"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        announce(ctx, "Library. BARD and Bookshare not yet configured.");
        announce(ctx, "Public domain texts available offline when configured.");
    }

    void on_exit(UiContext &ctx) override { announce(ctx, "Library closed"); }
    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}
    void on_text(const std::string &, UiContext &) override {}
    void on_control(keyboard::ControlKey, bool, UiContext &) override {}
};

class LocalSendApp final : public AppSession {
public:
    std::string id() const override { return "localsend"; }
    std::string label() const override { return "LocalSend"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        announce(ctx, "LocalSend not yet configured. See localsend.conf.");
    }

    void on_exit(UiContext &ctx) override { announce(ctx, "LocalSend closed"); }
    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}
    void on_text(const std::string &, UiContext &) override {}
    void on_control(keyboard::ControlKey, bool, UiContext &) override {}
};

class QuickStatusInline final : public AppSession {
public:
    std::string id() const override { return "quick_status"; }
    std::string label() const override { return "Quick Status"; }
    AppKind kind() const override { return AppKind::Inline; }

    void on_enter(UiContext &ctx) override
    {
        if (ctx.output != nullptr) {
            ctx.output->announce_quick_status();
        }
    }

    void on_exit(UiContext &) override {}
    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}
    void on_text(const std::string &, UiContext &) override {}
    void on_control(keyboard::ControlKey, bool, UiContext &) override {}
};

class MorseOutputInline final : public AppSession {
public:
    std::string id() const override { return "morse_output"; }
    std::string label() const override { return "Morse Code Output"; }
    AppKind kind() const override { return AppKind::Inline; }

    void on_enter(UiContext &ctx) override
    {
        if (ctx.output != nullptr) {
            ctx.output->set_morse_passive(true);
        }
        announce(ctx, "Morse output enabled");
    }

    void on_exit(UiContext &ctx) override
    {
        if (ctx.output != nullptr) {
            ctx.output->set_morse_passive(false);
        }
    }

    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}
    void on_text(const std::string &, UiContext &) override {}
    void on_control(keyboard::ControlKey, bool, UiContext &) override {}
};

class PaperNavInline final : public AppSession {
public:
    std::string id() const override { return "paper_nav"; }
    std::string label() const override { return "Paper Navigation"; }
    AppKind kind() const override { return AppKind::Inline; }

    void on_enter(UiContext &ctx) override
    {
        announce(ctx, "Paper navigation. Up or down to move lines.");
    }

    void on_exit(UiContext &) override {}
    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}
    void on_text(const std::string &, UiContext &) override {}

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed || ctx.motion == nullptr) {
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            ctx.motion->feed_lines(-1);
        } else if (key == keyboard::ControlKey::DpadDown) {
            ctx.motion->feed_lines(1);
        }
        if (ctx.coords != nullptr) {
            ctx.coords->mutable_state().y_line_index = ctx.motion->paper().y_line_index();
            ctx.coords->save();
        }
    }
};

class SaveExitInline final : public AppSession {
public:
    std::string id() const override { return "save_exit"; }
    std::string label() const override { return "Save and Exit"; }
    AppKind kind() const override { return AppKind::Inline; }

    void on_enter(UiContext &ctx) override
    {
        if (ctx.brf != nullptr) {
            ctx.brf->save();
        }
        if (ctx.coords != nullptr) {
            ctx.coords->save();
        }
        if (ctx.registry != nullptr) {
            ctx.registry->exit();
        }
        announce(ctx, "Saved and exited");
    }

    void on_exit(UiContext &) override {}
    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}
    void on_text(const std::string &, UiContext &) override {}
    void on_control(keyboard::ControlKey, bool, UiContext &) override {}
};

} // namespace

std::unique_ptr<AppSession> make_brailler_app()
{
    return std::make_unique<BraillerApp>();
}
std::unique_ptr<AppSession> make_calculator_app()
{
    return std::make_unique<CalculatorApp>();
}
std::unique_ptr<AppSession> make_transcriber_app()
{
    return std::make_unique<TranscriberApp>();
}
std::unique_ptr<AppSession> make_morse_learn_app()
{
    return std::make_unique<MorseLearnApp>();
}
std::unique_ptr<AppSession> make_network_app()
{
    return std::make_unique<NetworkApp>();
}
std::unique_ptr<AppSession> make_library_app()
{
    return std::make_unique<LibraryApp>();
}
std::unique_ptr<AppSession> make_localsend_app()
{
    return std::make_unique<LocalSendApp>();
}
std::unique_ptr<AppSession> make_quick_status_inline()
{
    return std::make_unique<QuickStatusInline>();
}
std::unique_ptr<AppSession> make_morse_output_inline()
{
    return std::make_unique<MorseOutputInline>();
}
std::unique_ptr<AppSession> make_paper_nav_inline()
{
    return std::make_unique<PaperNavInline>();
}
std::unique_ptr<AppSession> make_save_exit_inline()
{
    return std::make_unique<SaveExitInline>();
}

} // namespace braillatron::ui
