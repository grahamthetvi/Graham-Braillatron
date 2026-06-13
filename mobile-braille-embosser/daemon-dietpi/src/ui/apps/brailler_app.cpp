#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../../documents/edit_session.h"
#include "../../motion/motion_service.h"

#include <memory>
#include <regex>
#include <string>

namespace braillatron::ui {
namespace {

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

} // namespace

std::unique_ptr<AppSession> make_brailler_app()
{
    return std::make_unique<BraillerApp>();
}

} // namespace braillatron::ui
