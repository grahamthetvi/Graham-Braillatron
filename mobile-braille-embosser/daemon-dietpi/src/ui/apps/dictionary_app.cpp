#include "../../documents/dictionary_store.h"
#include "../layered_browse_list.h"
#include "../output_hub.h"
#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../../documents/liblouis_bridge.h"
#include "../../motion/motion_service.h"

#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

enum class Phase {
    Search,
    PickMatch,
    ReadDefinition,
};

class DictionaryApp final : public AppSession {
public:
    std::string id() const override { return "dictionary"; }
    std::string label() const override { return "Dictionary"; }
    AppKind kind() const override { return AppKind::Standalone; }

    bool browse_list_active() const override
    {
        return phase_ == Phase::PickMatch || phase_ == Phase::ReadDefinition;
    }

    const LayeredBrowseList *browse_list() const override
    {
        return browse_list_active() ? &browse_ : nullptr;
    }

    std::string composer_line() const override
    {
        return phase_ == Phase::Search ? query_buffer_ : std::string {};
    }

    std::string browse_breadcrumb() const override
    {
        switch (phase_) {
        case Phase::PickMatch:
            return "Matches";
        case Phase::ReadDefinition:
            return "Definitions";
        default:
            return {};
        }
    }

    void on_enter(UiContext &ctx) override
    {
        reset_session();
        if (!store_.open()) {
            announce(ctx, "Dictionary database not available.");
            return;
        }
        sync_chrome(ctx);
        announce(ctx, "Dictionary ready. Type a word and press Enter.");
    }

    void on_exit(UiContext &ctx) override
    {
        reset_session();
        store_.close();
        announce(ctx, "Dictionary closed");
    }

    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}

    bool buffers_braille_words() const override { return phase_ == Phase::Search; }

    void on_text(const std::string &text, UiContext &ctx) override
    {
        if (phase_ != Phase::Search || text.empty()) {
            return;
        }
        query_buffer_ += text;
        sync_chrome(ctx);
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed) {
            return;
        }

        switch (phase_) {
        case Phase::Search:
            handle_search_control(key, ctx);
            break;
        case Phase::PickMatch:
            handle_pick_control(key, ctx);
            break;
        case Phase::ReadDefinition:
            handle_read_control(key, ctx);
            break;
        }
    }

private:
    void reset_session()
    {
        phase_ = Phase::Search;
        query_buffer_.clear();
        matches_.clear();
        match_index_ = 0;
        entries_.clear();
        entry_index_ = 0;
        browse_.clear();
    }

    void sync_browse_list()
    {
        if (phase_ == Phase::PickMatch) {
            browse_.set_items(matches_, match_index_);
            browse_.set_container_name("Matches");
            return;
        }
        if (phase_ == Phase::ReadDefinition) {
            std::vector<std::string> labels;
            labels.reserve(entries_.size());
            for (const auto &entry : entries_) {
                std::string label = entry.word;
                if (!entry.part_of_speech.empty()) {
                    label += ", " + entry.part_of_speech;
                }
                labels.push_back(std::move(label));
            }
            browse_.set_items(std::move(labels), entry_index_);
            browse_.set_container_name("Definitions");
        }
    }

    void announce_browse_focus(UiContext &ctx, bool at_boundary)
    {
        browse_.announce_focus(ctx.output, at_boundary);
    }

    void handle_search_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!query_buffer_.empty()) {
                query_buffer_.pop_back();
                sync_chrome(ctx);
            }
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }
        if (query_buffer_.empty()) {
            announce(ctx, "Type a word first");
            return;
        }

        entries_ = store_.lookup(query_buffer_);
        if (!entries_.empty()) {
            entry_index_ = 0;
            phase_ = Phase::ReadDefinition;
            sync_browse_list();
            sync_chrome(ctx);
            announce_entry(ctx);
            return;
        }

        matches_ = store_.prefix_matches(query_buffer_);
        if (matches_.empty()) {
            announce(ctx, "No matches for " + query_buffer_);
            return;
        }
        if (matches_.size() == 1) {
            query_buffer_ = matches_.front();
            entries_ = store_.lookup(query_buffer_);
            entry_index_ = 0;
            phase_ = Phase::ReadDefinition;
            sync_browse_list();
            sync_chrome(ctx);
            announce_entry(ctx);
            return;
        }

        match_index_ = 0;
        phase_ = Phase::PickMatch;
        sync_browse_list();
        sync_chrome(ctx);
        announce_match(ctx);
    }

    void handle_pick_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Search;
            matches_.clear();
            match_index_ = 0;
            browse_.clear();
            sync_chrome(ctx);
            announce(ctx, "Search. " + query_buffer_);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && match_index_ > 0) {
            --match_index_;
            browse_.set_focus(match_index_);
            sync_chrome(ctx);
            announce_match(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown && match_index_ + 1 < matches_.size()) {
            ++match_index_;
            browse_.set_focus(match_index_);
            sync_chrome(ctx);
            announce_match(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter || matches_.empty()) {
            return;
        }

        query_buffer_ = matches_[match_index_];
        entries_ = store_.lookup(query_buffer_);
        entry_index_ = 0;
        phase_ = Phase::ReadDefinition;
        sync_browse_list();
        sync_chrome(ctx);
        announce_entry(ctx);
    }

    void handle_read_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Search;
            entries_.clear();
            entry_index_ = 0;
            browse_.clear();
            sync_chrome(ctx);
            announce(ctx, "Search. " + query_buffer_);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && entry_index_ > 0) {
            --entry_index_;
            browse_.set_focus(entry_index_);
            sync_chrome(ctx);
            announce_entry(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown && entry_index_ + 1 < entries_.size()) {
            ++entry_index_;
            browse_.set_focus(entry_index_);
            sync_chrome(ctx);
            announce_entry(ctx);
            return;
        }
        if (key == keyboard::ControlKey::Enter && ctx.motion != nullptr && ctx.braille != nullptr &&
            config_.emboss_enabled && entry_index_ < entries_.size()) {
            const auto &entry = entries_[entry_index_];
            const std::string text = entry.word + ". " + entry.definition;
            ctx.motion->emboss_text(text, *ctx.braille);
            announce(ctx, "Embossing definition");
        }
    }

    void announce_match(UiContext &ctx)
    {
        announce(ctx, "Match " + std::to_string(match_index_ + 1) + " of " +
                           std::to_string(matches_.size()) + ". " + matches_[match_index_]);
    }

    void announce_entry(UiContext &ctx)
    {
        if (entry_index_ >= entries_.size()) {
            return;
        }
        const auto &entry = entries_[entry_index_];
        std::string message = entry.word;
        if (!entry.part_of_speech.empty()) {
            message += ", " + entry.part_of_speech;
        }
        message += ". " + entry.definition;
        if (entries_.size() > 1) {
            message = "Definition " + std::to_string(entry_index_ + 1) + " of " +
                      std::to_string(entries_.size()) + ". " + message;
        }
        announce(ctx, message);
    }

    documents::DictionaryConfig config_ =
        documents::load_dictionary_config("/etc/braillatron/dictionary.conf");
    documents::DictionaryStore store_ {config_};
    LayeredBrowseList browse_;
    Phase phase_ = Phase::Search;
    std::string query_buffer_;
    std::vector<std::string> matches_;
    size_t match_index_ = 0;
    std::vector<documents::DictionaryEntry> entries_;
    size_t entry_index_ = 0;
};

} // namespace

std::unique_ptr<AppSession> make_dictionary_app()
{
    return std::make_unique<DictionaryApp>();
}

} // namespace braillatron::ui
