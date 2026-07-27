#include "app_session.h"
#include "app_util.h"
#include "calculator_braille.h"
#include "calculator_eval.h"
#include "ui_context.h"

#include "../output_hub.h"

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
        result_line_.clear();
        mode_ = CalcAudioMode::Char;
        announce(ctx, "Calculator ready. Character mode. Menu to change audio mode.");
        refresh_display(ctx);
    }

    void on_exit(UiContext &ctx) override
    {
        buffer_.clear();
        result_line_.clear();
        announce(ctx, "Calculator closed");
    }

    void on_poll(UiContext &) override {}

    void on_chord(uint8_t dot_mask, UiContext &ctx) override
    {
        const auto ch = calculator_char_from_dot_mask(dot_mask);
        if (!ch.has_value()) {
            return;
        }
        on_text(std::string(1, *ch), ctx);
    }

    std::string composer_line() const override { return buffer_; }

    std::string result_line() const override { return result_line_; }

    void on_text(const std::string &text, UiContext &ctx) override
    {
        for (char ch : text) {
            if (!is_valid_calculator_char(ch)) {
                announce(ctx, "Invalid character");
                continue;
            }

            if (ch == ' ') {
                handle_space(ctx);
                continue;
            }

            result_line_.clear();
            buffer_.push_back(ch);
            if (mode_ == CalcAudioMode::Char) {
                announce(ctx, calculator_char_spoken(ch));
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
            handle_backspace(ctx);
            return;
        }

        if (key == keyboard::ControlKey::DpadDown) {
            clear_all(ctx);
            return;
        }

        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        evaluate_buffer(ctx, true);
    }

private:
    static std::string trim_trailing_spaces(const std::string &value)
    {
        size_t end = value.size();
        while (end > 0 && value[end - 1] == ' ') {
            --end;
        }
        return value.substr(0, end);
    }

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

    void handle_backspace(UiContext &ctx)
    {
        if (!buffer_.empty()) {
            const char removed = buffer_.back();
            buffer_.pop_back();
            result_line_.clear();
            refresh_display(ctx);
            if (mode_ == CalcAudioMode::Char) {
                announce(ctx, "Deleted " + calculator_char_spoken(removed));
            }
            return;
        }

        if (!result_line_.empty()) {
            result_line_.clear();
            refresh_display(ctx);
            if (mode_ == CalcAudioMode::Char) {
                announce(ctx, "Result cleared");
            }
        }
    }

    void clear_all(UiContext &ctx)
    {
        if (buffer_.empty() && result_line_.empty()) {
            return;
        }
        buffer_.clear();
        result_line_.clear();
        refresh_display(ctx);
        announce(ctx, "Cleared");
    }

    void handle_space(UiContext &ctx)
    {
        const std::string trimmed = trim_trailing_spaces(buffer_);
        if (!trimmed.empty()) {
            const auto outcome = evaluate_calculator_expression_outcome(trimmed);
            if (outcome.value.has_value()) {
                if (mode_ != CalcAudioMode::Silent) {
                    announce(ctx, "Equation: " + trimmed);
                }
                finish_evaluation(ctx, trimmed, *outcome.value);
                return;
            }
            if (outcome.error != CalculatorEvalError::None) {
                if (mode_ != CalcAudioMode::Silent) {
                    announce(ctx, "Equation: " + trimmed);
                }
                announce(ctx, calculator_eval_error_message(outcome.error));
                return;
            }
        }

        if (mode_ != CalcAudioMode::Silent) {
            announce(ctx, trimmed.empty() ? "Equation empty" : "Equation: " + buffer_);
        }
        if (mode_ == CalcAudioMode::SpaceAffirm && !trimmed.empty()) {
            try_emboss_text(ctx, trimmed, false);
        }

        buffer_.push_back(' ');
        refresh_display(ctx);
    }

    void evaluate_buffer(UiContext &ctx, bool from_enter)
    {
        const std::string trimmed = trim_trailing_spaces(buffer_);
        if (trimmed.empty()) {
            announce(ctx, from_enter ? "Enter an equation first" : "Equation empty");
            return;
        }

        if (mode_ != CalcAudioMode::Silent) {
            announce(ctx, "Equation: " + trimmed);
        }

        const auto outcome = evaluate_calculator_expression_outcome(trimmed);
        if (!outcome.value.has_value()) {
            announce(ctx, calculator_eval_error_message(outcome.error));
            return;
        }

        finish_evaluation(ctx, trimmed, *outcome.value);
    }

    void finish_evaluation(UiContext &ctx, const std::string &equation, double value)
    {
        const std::string result_text = format_calculator_result(value);

        if (mode_ != CalcAudioMode::Silent) {
            announce(ctx, "Result: " + result_text);
        }

        result_line_ = result_text;
        buffer_ = result_text;
        refresh_display(ctx);
        try_emboss_text(ctx, equation + " = " + result_text, true);
    }

    void try_emboss_text(UiContext &ctx, const std::string &text, bool advance_paper)
    {
        const bool emboss_enabled =
            ctx.output != nullptr && ctx.output->ui_config().embosser_enabled;
        if (!emboss_enabled || !emboss_hardware_ready(ctx)) {
            if (advance_paper) {
                announce(ctx, "Embossing not available");
            }
            return;
        }

        ctx.motion->emboss_text(text, *ctx.braille);
        if (advance_paper) {
            ctx.motion->advance_line();
        }
        announce(ctx, advance_paper ? "Embossing result" : "Embossing equation");
    }

    std::string buffer_;
    std::string result_line_;
    CalcAudioMode mode_ = CalcAudioMode::Char;
};

} // namespace

std::unique_ptr<AppSession> make_calculator_app()
{
    return std::make_unique<CalculatorApp>();
}

} // namespace braillatron::ui
