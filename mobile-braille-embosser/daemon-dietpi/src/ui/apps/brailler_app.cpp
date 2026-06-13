#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../output_hub.h"

#include "../../documents/edit_session.h"
#include "../../motion/motion_service.h"

#include <cctype>
#include <deque>
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
        brf_ = ctx.brf;
        edit_ = ctx.edit;
        output_ = ctx.output;

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

        dictation_queue_.clear();
        dictation_paused_announced_ = false;
        register_dictation_handler();

        if (output_ != nullptr && output_->ui_config().document_dictation_enabled) {
            announce(ctx, "Brailler ready. Hold speech button to dictate.");
        } else {
            announce(ctx, "Brailler ready");
        }
    }

    void on_exit(UiContext &ctx) override
    {
        if (output_ != nullptr) {
            output_->set_stt_transcript_handler(nullptr);
        }
        dictation_queue_.clear();
        dictation_paused_announced_ = false;
        brf_ = nullptr;
        edit_ = nullptr;
        output_ = nullptr;

        if (ctx.brf != nullptr) {
            ctx.brf->save();
        }
        if (ctx.coords != nullptr) {
            ctx.coords->mutable_state().active_app_id.clear();
            ctx.coords->save();
        }
        announce(ctx, "Brailler closed");
    }

    void on_poll(UiContext &) override
    {
        flush_dictation_queue();
    }

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

private:
    bool dictation_enabled() const
    {
        return output_ != nullptr && output_->ui_config().document_dictation_enabled
               && output_->ui_config().stt_enabled;
    }

    bool can_inject_dictation() const
    {
        if (edit_ == nullptr) {
            return true;
        }
        const documents::EditState state = edit_->state();
        return state != documents::EditState::ReplacementLine
               && state != documents::EditState::LineReview
               && state != documents::EditState::AwaitFullCell;
    }

    void register_dictation_handler()
    {
        if (output_ == nullptr || !dictation_enabled()) {
            return;
        }

        output_->set_stt_transcript_handler([this](const std::string &text, bool is_final) {
            if (!is_final || text.empty() || !dictation_enabled()) {
                return;
            }

            if (!can_inject_dictation()) {
                if (dictation_queue_.size() >= queue_limit_) {
                    if (output_ != nullptr) {
                        output_->announce_message("Dictation buffer full.");
                        output_->play_boundary_haptic();
                    }
                    return;
                }
                dictation_queue_.push_back(text);
                if (!dictation_paused_announced_ && output_ != nullptr) {
                    output_->announce_message("Dictation paused during edit.");
                    dictation_paused_announced_ = true;
                }
                return;
            }

            inject_dictation_text(text);
        });
    }

    void inject_dictation_text(const std::string &text)
    {
        if (text.empty() || brf_ == nullptr) {
            return;
        }

        if (!brf_->lines().empty()) {
            const std::string &current = brf_->lines().back();
            if (!current.empty() && !std::isspace(static_cast<unsigned char>(current.back()))
                && !std::isspace(static_cast<unsigned char>(text.front()))) {
                brf_->append_char(' ');
            }
        }

        for (char ch : text) {
            brf_->append_char(ch);
        }
        brf_->save();
        dictation_paused_announced_ = false;
    }

    void flush_dictation_queue()
    {
        if (!can_inject_dictation() || dictation_queue_.empty()) {
            return;
        }

        while (!dictation_queue_.empty() && can_inject_dictation()) {
            inject_dictation_text(dictation_queue_.front());
            dictation_queue_.pop_front();
        }
    }

    documents::BrfStore *brf_ = nullptr;
    documents::EditSession *edit_ = nullptr;
    OutputHub *output_ = nullptr;
    std::deque<std::string> dictation_queue_;
    bool dictation_paused_announced_ = false;
    static constexpr size_t queue_limit_ = 8;
};

} // namespace

std::unique_ptr<AppSession> make_brailler_app()
{
    return std::make_unique<BraillerApp>();
}

} // namespace braillatron::ui
