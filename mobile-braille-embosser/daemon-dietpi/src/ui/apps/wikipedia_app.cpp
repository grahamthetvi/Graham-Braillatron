#include "../../net/wikipedia_client.h"
#include "../output_hub.h"
#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../../documents/liblouis_bridge.h"
#include "../../motion/motion_service.h"

#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

constexpr size_t kMaxAnnounceLen = 300;

std::string truncate_for_tts(const std::string &text)
{
    if (text.size() <= kMaxAnnounceLen) {
        return text;
    }
    return text.substr(0, kMaxAnnounceLen) + "...";
}

enum class Phase {
    Search,
    PickResult,
    Read,
};

class WikipediaApp final : public AppSession {
public:
    std::string id() const override { return "wikipedia"; }
    std::string label() const override { return "Wikipedia"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        reset_session();
        announce(ctx, "Wikipedia ready. Type a topic and press Enter.");
    }

    void on_exit(UiContext &ctx) override
    {
        reset_session();
        announce(ctx, "Wikipedia closed");
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
        refresh_search_input(ctx);
    }

    std::string composer_line() const override
    {
        if (phase_ != Phase::Search) {
            return {};
        }
        return query_buffer_;
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
        case Phase::PickResult:
            handle_pick_control(key, ctx);
            break;
        case Phase::Read:
            handle_read_control(key, ctx);
            break;
        }
    }

private:
    void reset_session()
    {
        phase_ = Phase::Search;
        query_buffer_.clear();
        results_.clear();
        result_index_ = 0;
        lines_.clear();
        line_index_ = 0;
    }

    void handle_search_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!query_buffer_.empty()) {
                query_buffer_.pop_back();
                refresh_search_input(ctx);
            }
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }
        if (query_buffer_.empty()) {
            announce(ctx, "Type a topic first");
            return;
        }

        announce(ctx, "Searching");
        const auto results = client_.search(query_buffer_);
        if (!results.has_value()) {
            announce(ctx, "Network error");
            return;
        }
        if (results->empty()) {
            announce(ctx, "No results");
            return;
        }

        results_ = *results;
        result_index_ = 0;
        phase_ = Phase::PickResult;
        if (ctx.output != nullptr) {
            ctx.output->sync_chrome(false);
        }
        announce_result(ctx);
    }

    void handle_pick_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Search;
            results_.clear();
            result_index_ = 0;
            refresh_search_input(ctx);
            announce(ctx, "Search. " + query_buffer_);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            if (result_index_ > 0) {
                --result_index_;
                announce_result(ctx);
            }
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            if (result_index_ + 1 < results_.size()) {
                ++result_index_;
                announce_result(ctx);
            }
            return;
        }
        if (key != keyboard::ControlKey::Enter || results_.empty()) {
            return;
        }

        const std::string &title = results_[result_index_].title;
        announce(ctx, "Fetching article");
        const auto article = client_.fetch_plaintext(title);
        if (!article.has_value()) {
            announce(ctx, "Network error");
            return;
        }
        if (article->empty()) {
            announce(ctx, "Article not found");
            return;
        }

        lines_ = client_.split_into_lines(*article);
        if (lines_.empty()) {
            announce(ctx, "Article not found");
            return;
        }

        line_index_ = 0;
        phase_ = Phase::Read;
        announce_line(ctx);
    }

    void handle_read_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::PickResult;
            lines_.clear();
            line_index_ = 0;
            announce_result(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            if (line_index_ > 0) {
                --line_index_;
                announce_line(ctx);
            }
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            if (line_index_ + 1 < lines_.size()) {
                ++line_index_;
                announce_line(ctx);
            }
            return;
        }
        if (key != keyboard::ControlKey::Enter || lines_.empty()) {
            return;
        }
        if (ctx.motion != nullptr && ctx.braille != nullptr) {
            ctx.motion->emboss_text(lines_[line_index_], *ctx.braille);
        }
    }

    void announce_result(UiContext &ctx)
    {
        if (results_.empty()) {
            return;
        }
        const net::WikipediaSearchResult &entry = results_[result_index_];
        std::string message = "Result " + std::to_string(result_index_ + 1) + " of " +
                              std::to_string(results_.size()) + ". " + entry.title;
        if (!entry.description.empty()) {
            message += ". " + truncate_for_tts(entry.description);
        }
        announce(ctx, message);
    }

    void announce_line(UiContext &ctx)
    {
        if (lines_.empty()) {
            return;
        }
        const std::string prefix = "Line " + std::to_string(line_index_ + 1) + " of " +
                                   std::to_string(lines_.size()) + ". ";
        announce(ctx, prefix + truncate_for_tts(lines_[line_index_]));
    }

    void refresh_search_input(UiContext &ctx)
    {
        if (ctx.output == nullptr) {
            return;
        }
        ctx.output->sync_chrome(false);
        if (!query_buffer_.empty()) {
            ctx.output->announce_spoken("Search: " + query_buffer_);
        }
    }

    Phase phase_ = Phase::Search;
    std::string query_buffer_;
    std::vector<net::WikipediaSearchResult> results_;
    size_t result_index_ = 0;
    std::vector<std::string> lines_;
    size_t line_index_ = 0;
    net::WikipediaClient client_;
};

} // namespace

std::unique_ptr<AppSession> make_wikipedia_app()
{
    return std::make_unique<WikipediaApp>();
}

} // namespace braillatron::ui
