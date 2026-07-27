#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../../motion/motion_service.h"

#include <memory>
#include <string>

namespace braillatron::ui {
namespace {

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
        sync_coords_from_motion(ctx);
    }
};

} // namespace

std::unique_ptr<AppSession> make_paper_nav_inline()
{
    return std::make_unique<PaperNavInline>();
}

} // namespace braillatron::ui
