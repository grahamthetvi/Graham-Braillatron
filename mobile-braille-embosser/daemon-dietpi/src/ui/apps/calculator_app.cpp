#include "app_session.h"
#include "app_util.h"
#include "calculator_eval.h"
#include "ui_context.h"

#include "../output_hub.h"

#include "../../motion/motion_service.h"

#include <memory>
#include <string>

namespace braillatron::ui {
namespace {

enum class CalcAudioMode { Char, Silent, SpaceAffirm };

class CalculatorApp final : public AppSession {
public:
    std::string id() const override { return "calculator"; }
    std::string label() const override { return "Calculator"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        buffer_.clear();
        mode_ = CalcAudioMode::Char;
        announce(ctx, "Calculator ready.");
    }

    void on_exit(UiContext &ctx) override { announce(ctx, "Calculator closed"); }
    void on_poll(UiContext &) override {}

    void on_chord(uint8_t, UiContext &) override {}

    std::string composer_line() const override { return buffer_; }

    void on_text(const std::string &text, UiContext &ctx) override
    {
        for (char ch : text) {
            if (!is_valid_calculator_char(ch)) {
                announce(ctx, "Invalid character");
                continue;
            }

            if (ch == ' ') {
                if (mode_ != CalcAudioMode::Silent) {
                    announce(ctx, "Equation: " + buffer_);
                }
                if (ctx.motion != nullptr && ctx.braille != nullptr &&
                    mode_ == CalcAudioMode::SpaceAffirm) {
                    ctx.motion->emboss_text(buffer_, *ctx.braille);
                }
                buffer_.push_back(ch);
                refresh_display(ctx);
                continue;
            }

            buffer_.push_back(ch);
            if (mode_ == CalcAudioMode::Char) {
                announce(ctx, std::string(1, ch));
            }
            refresh_display(ctx);
        }
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed) {
            return;
        }

        if (key == keyboard::ControlKey::Menu) {
            cycle_audio_mode(ctx);
            return;
        }

        if (key == keyboard::ControlKey::Backspace) {
            if (buffer_.empty()) {
                return;
            }
            const char removed = buffer_.back();
            buffer_.pop_back();
            refresh_display(ctx);
            if (mode_ == CalcAudioMode::Char) {
                announce(ctx, std::string(1, removed));
            }
            return;
        }

        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        if (buffer_.empty()) {
            announce(ctx, "Enter an equation first");
            return;
        }

        const auto result = evaluate_calculator_expression(buffer_);
        if (!result.has_value()) {
            announce(ctx, "Invalid equation");
            return;
        }

        const std::string result_text = format_calculator_result(*result);
        announce(ctx, "Result: " + result_text);
        buffer_ = result_text;
        refresh_display(ctx);
    }

private:
    static std::string audio_mode_label(CalcAudioMode mode)
    {
        switch (mode) {
        case CalcAudioMode::Char:
            return "Character mode";
        case CalcAudioMode::Silent:
            return "Silent mode";
        case CalcAudioMode::SpaceAffirm:
            return "Space affirm mode";
        }
        return {};
    }

    void cycle_audio_mode(UiContext &ctx)
    {
        switch (mode_) {
        case CalcAudioMode::Char:
            mode_ = CalcAudioMode::Silent;
            break;
        case CalcAudioMode::Silent:
            mode_ = CalcAudioMode::SpaceAffirm;
            break;
        case CalcAudioMode::SpaceAffirm:
            mode_ = CalcAudioMode::Char;
            break;
        }
        announce(ctx, audio_mode_label(mode_));
    }

    void refresh_display(UiContext &ctx)
    {
        if (ctx.output != nullptr) {
            ctx.output->sync_chrome(false);
        }
    }

    std::string buffer_;
    CalcAudioMode mode_ = CalcAudioMode::Char;
};

} // namespace

std::unique_ptr<AppSession> make_calculator_app()
{
    return std::make_unique<CalculatorApp>();
}

} // namespace braillatron::ui
