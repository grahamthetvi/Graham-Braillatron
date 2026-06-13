#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../output_hub.h"

#include <memory>
#include <string>

namespace braillatron::ui {
namespace {

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

} // namespace

std::unique_ptr<AppSession> make_morse_output_inline()
{
    return std::make_unique<MorseOutputInline>();
}

} // namespace braillatron::ui
