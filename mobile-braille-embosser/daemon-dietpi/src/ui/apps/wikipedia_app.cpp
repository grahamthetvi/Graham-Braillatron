#include "../../net/wikipedia_client.h"
#include "../layered_browse_list.h"
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
constexpr size_t kMaxBrowseLabelLen = 72;

std::string truncate_for_tts(const std::string &text)
{
    if (text.size() <= kMaxAnnounceLen) {
        return text;
    }
    return text.substr(0, kMaxAnnounceLen) + "...";
}

std::string truncate_for_browse(const std::string &text)
{
    if (text.size() <= kMaxBrowseLabelLen) {
        return text;
    }
    return text.substr(0, kMaxBrowseLabelLen - 3) + "...";
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

    bool browse_list_active() const override
    {
        return phase_ == Phase::PickResult || phase_ == Phase::Read;
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
        case Phase::PickResult:
            return "Results";
        case Phase::Read:
            return article_title_.empty() ? "Article" : article_title_;
        default:
            return {};
        }
    }

    void on_enter(UiContext &ctx) override
    {
        reset_session();
        sync_chrome(ctx);
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
        sync_chrome(ctx);
    }

    void on_menu_action(const std::string &action, UiContext &ctx) override
    {
        if (action != "print") {
            return;
        }
        print_article(ctx);
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
        article_title_.clear();
        browse_.clear();
    }

    void sync_browse_list()
    {
        if (phase_ == Phase::PickResult) {
            std::vector<std::string> labels;
            labels.reserve(results_.size());
            for (const auto &entry : results_) {
                labels.push_back(entry.title);
            }
            browse_.set_items(std::move(labels), result_index_);
            browse_.set_container_name("Results");
            return;
        }
        if (phase_ == Phase::Read) {
            std::vector<std::string> labels;
            labels.reserve(lines_.size());
            for (const auto &line : lines_) {
                labels.push_back(truncate_for_browse(line));
            }
            browse_.set_items(std::move(labels), line_index_);
            browse_.set_container_name(article_title_.empty() ? "Article" : article_title_);
        }
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
        sync_browse_list();
        sync_chrome(ctx);
        announce_result(ctx);
    }

    void handle_pick_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Search;
            results_.clear();
            result_index_ = 0;
            browse_.clear();
            sync_chrome(ctx);
            announce(ctx, "Search. " + query_buffer_);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && result_index_ > 0) {
            --result_index_;
            browse_.set_focus(result_index_);
            sync_chrome(ctx);
            announce_result(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown && result_index_ + 1 < results_.size()) {
            ++result_index_;
            browse_.set_focus(result_index_);
            sync_chrome(ctx);
            announce_result(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter || results_.empty()) {
            return;
        }

        open_article(results_[result_index_].title, ctx);
    }

    void handle_read_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::PickResult;
            lines_.clear();
            line_index_ = 0;
            article_title_.clear();
            sync_browse_list();
            sync_chrome(ctx);
            announce_result(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && line_index_ > 0) {
            --line_index_;
            browse_.set_focus(line_index_);
            sync_chrome(ctx);
            announce_line(ctx);
            return;
        }
        if (key == keyboard::ControlKey::DpadDown && line_index_ + 1 < lines_.size()) {
            ++line_index_;
            browse_.set_focus(line_index_);
            sync_chrome(ctx);
            announce_line(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter || lines_.empty()) {
            return;
        }
        if (ctx.motion != nullptr && ctx.braille != nullptr) {
            ctx.motion->emboss_text(lines_[line_index_], *ctx.braille);
            announce(ctx, "Embossing line");
        }
    }

    void open_article(const std::string &title, UiContext &ctx)
    {
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

        article_title_ = title;
        line_index_ = 0;
        phase_ = Phase::Read;
        sync_browse_list();
        sync_chrome(ctx);
        announce_line(ctx);
    }

    void print_article(UiContext &ctx)
    {
        if (phase_ != Phase::Read || lines_.empty()) {
            announce(ctx, "Nothing to print");
            return;
        }
        if (ctx.motion == nullptr || ctx.braille == nullptr) {
            announce(ctx, "Embossing not available");
            return;
        }

        std::string text;
        if (!article_title_.empty()) {
            text = article_title_ + "\n\n";
        }
        for (size_t i = 0; i < lines_.size(); ++i) {
            if (i > 0) {
                text += '\n';
            }
            text += lines_[i];
        }
        ctx.motion->emboss_text(text, *ctx.braille);
        announce(ctx, "Printing article");
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

    Phase phase_ = Phase::Search;
    std::string query_buffer_;
    std::vector<net::WikipediaSearchResult> results_;
    size_t result_index_ = 0;
    std::vector<std::string> lines_;
    size_t line_index_ = 0;
    std::string article_title_;
    LayeredBrowseList browse_;
    net::WikipediaClient client_;
};

} // namespace

std::unique_ptr<AppSession> make_wikipedia_app()
{
    return std::make_unique<WikipediaApp>();
}

} // namespace braillatron::ui
