#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include <memory>
#include <string>

namespace braillatron::ui {
namespace {

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

} // namespace

std::unique_ptr<AppSession> make_localsend_app()
{
    return std::make_unique<LocalSendApp>();
}

} // namespace braillatron::ui
