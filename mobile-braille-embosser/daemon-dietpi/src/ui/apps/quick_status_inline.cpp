#include "app_session.h"
#include "ui_context.h"

#include "../output_hub.h"

#include <memory>
#include <string>

namespace braillatron::ui {
namespace {

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

} // namespace

std::unique_ptr<AppSession> make_quick_status_inline()
{
    return std::make_unique<QuickStatusInline>();
}

} // namespace braillatron::ui
