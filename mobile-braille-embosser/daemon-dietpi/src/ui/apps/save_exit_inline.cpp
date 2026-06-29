#include "app_registry.h"
#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../../documents/library_store.h"

#include <memory>
#include <string>

namespace braillatron::ui {
namespace {

documents::LibraryStoreConfig library_config()
{
    return documents::load_library_store_config("/etc/braillatron/library.conf");
}

class SaveExitInline final : public AppSession {
public:
    std::string id() const override { return "save_exit"; }
    std::string label() const override { return "Save and Exit"; }
    AppKind kind() const override { return AppKind::Inline; }

    void on_enter(UiContext &ctx) override
    {
        if (ctx.brf != nullptr) {
            ctx.brf->save();
            const std::string text = ctx.brf->full_text();
            if (!text.empty()) {
                documents::LibraryStore store(library_config());
                store.refresh();
                if (store.save_document_text(text)) {
                    announce(ctx, "Saved to library");
                }
            }
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

std::unique_ptr<AppSession> make_save_exit_inline()
{
    return std::make_unique<SaveExitInline>();
}

} // namespace braillatron::ui
