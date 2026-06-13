#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

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

} // namespace

std::unique_ptr<AppSession> make_calculator_app()
{
    return std::make_unique<CalculatorApp>();
}

} // namespace braillatron::ui
