#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include <memory>
#include <string>

namespace braillatron::ui {
namespace {

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

} // namespace

std::unique_ptr<AppSession> make_library_app()
{
    return std::make_unique<LibraryApp>();
}

} // namespace braillatron::ui
