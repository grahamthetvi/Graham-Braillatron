#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../output_hub.h"

#include "../../documents/edit_session.h"
#include "../../documents/liblouis_bridge.h"
#include "../../motion/motion_service.h"

#include <cctype>
#include <deque>
#include <memory>
#include <regex>
#include <string>
#include <vector>

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
        braille_input_ = ctx.braille_input;

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

        clear_pending_chords();
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
        clear_pending_chords();
        dictation_queue_.clear();
        dictation_paused_announced_ = false;
        brf_ = nullptr;
        edit_ = nullptr;
        output_ = nullptr;
        braille_input_ = nullptr;

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
            const documents::EditState state = ctx.edit->state();
            if (state == documents::EditState::LineReview ||
                state == documents::EditState::AwaitFullCell) {
                ctx.edit->on_full_cell(dot_mask);
                return;
            }
        }

        if (dot_mask == 0 || !can_buffer_chords()) {
            return;
        }

        pending_chords_.push_back(dot_mask);
        rebuild_pending_preview();
        sync_display(ctx);
    }

    void on_text(const std::string &text, UiContext &ctx) override
    {
        if (ctx.brf == nullptr) {
            return;
        }

        if (ctx.edit != nullptr && ctx.edit->state() == documents::EditState::ReplacementLine) {
            for (char ch : text) {
                if (ch != ' ') {
                    continue;
                }
                const std::string word = commit_pending_word();
                if (!word.empty()) {
                    ctx.edit->on_replacement_chord(0, word);
                    sync_display(ctx);
                }
            }
            return;
        }

        for (char ch : text) {
            if (ch == ' ') {
                append_committed_word(ctx);
                ctx.brf->append_char(' ');
            } else {
                append_committed_word(ctx);
                ctx.brf->append_char(ch);
            }
        }
        ctx.brf->save();
        sync_display(ctx);

        static const std::regex worksheet_pattern(R"((Name|Date)\s*:)",
                                                  std::regex::icase);
        if (std::regex_search(ctx.brf->full_text(), worksheet_pattern)) {
            announce(ctx, "Worksheet session recording");
        }
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed) {
            return;
        }

        if (key == keyboard::ControlKey::Backspace) {
            if (!pending_chords_.empty()) {
                pending_chords_.pop_back();
                rebuild_pending_preview();
                sync_display(ctx);
                return;
            }
            if (ctx.brf != nullptr) {
                ctx.brf->backspace();
                ctx.brf->save();
                sync_display(ctx);
            }
            return;
        }

        if (key != keyboard::ControlKey::Enter) {
            return;
        }
        if (ctx.edit != nullptr && ctx.brf != nullptr) {
            ctx.edit->begin_line_review(ctx.brf->line_count() > 0 ? ctx.brf->line_count() - 1 : 0);
        }
    }

    std::string composer_line() const override
    {
        std::string line;
        if (brf_ != nullptr && !brf_->lines().empty()) {
            line = brf_->lines().back();
        }
        line += pending_preview_;
        return line;
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

    bool can_buffer_chords() const
    {
        if (edit_ == nullptr) {
            return true;
        }
        const documents::EditState state = edit_->state();
        return state == documents::EditState::EmbossMode
               || state == documents::EditState::ReplacementLine;
    }

    void clear_pending_chords()
    {
        pending_chords_.clear();
        pending_preview_.clear();
    }

    void rebuild_pending_preview()
    {
        pending_preview_.clear();
        if (braille_input_ == nullptr) {
            return;
        }

        for (uint8_t dot_mask : pending_chords_) {
            const auto cell = braille_input_->translate_backward_dot_uncontracted(dot_mask);
            if (!cell.has_value() || cell->empty()) {
                pending_preview_.push_back('?');
                continue;
            }
            pending_preview_ += *cell;
        }
    }

    std::string commit_pending_word()
    {
        if (pending_chords_.empty()) {
            return {};
        }

        std::string word;
        if (braille_input_ != nullptr) {
            if (const auto translated = braille_input_->translate_backward_cells(pending_chords_)) {
                word = *translated;
            }
        }
        if (word.empty()) {
            word = pending_preview_;
        }

        clear_pending_chords();
        return word;
    }

    void append_committed_word(UiContext &ctx)
    {
        if (ctx.brf == nullptr) {
            return;
        }
        const std::string word = commit_pending_word();
        for (char ch : word) {
            ctx.brf->append_char(ch);
        }
    }

    void sync_display(UiContext &ctx)
    {
        if (ctx.output != nullptr) {
            ctx.output->sync_chrome(false);
        }
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
    documents::BrailleTranslationService *braille_input_ = nullptr;
    OutputHub *output_ = nullptr;
    std::vector<uint8_t> pending_chords_;
    std::string pending_preview_;
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
