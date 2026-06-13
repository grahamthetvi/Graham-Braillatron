#include "../../connect/connect_client.h"
#include "../../connect/json_utils.h"
#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../output_hub.h"

#include "../../keyboard/chord_engine.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

enum class YoutubeState { Search, Results, Playing };

struct YoutubeResultItem {
    std::string title;
    std::string url;
};

class YouTubeApp final : public AppSession {
public:
    std::string id() const override { return "youtube"; }
    std::string label() const override { return "YouTube"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        state_ = YoutubeState::Search;
        query_.clear();
        results_.clear();
        selected_ = 0;
        if (ctx.connect != nullptr) {
            const std::string status = ctx.connect->request("accounts.status");
            if (braillatron::connect::json_get_bool(status, "youtube_cookies", false)) {
                announce(ctx, "YouTube ready. Enter search query.");
            } else {
                announce(ctx,
                         "YouTube sign in required. Copy cookies to credentials incoming folder.");
            }
        } else {
            announce(ctx, "Connectivity unavailable.");
        }
    }

    void on_exit(UiContext &ctx) override
    {
        if (ctx.connect != nullptr) {
            ctx.connect->request("youtube.stop");
        }
        announce(ctx, "YouTube closed");
    }

    void on_poll(UiContext &ctx) override
    {
        if (ctx.connect == nullptr) {
            return;
        }
        ctx.connect->poll_events([this, &ctx](const braillatron::connect::ConnectEvent &event) {
            if (event.type == "youtube.ended") {
                state_ = YoutubeState::Results;
            }
            if (event.type == "youtube.playing" && ctx.output != nullptr) {
                ctx.output->set_media_playing(true);
            }
            if (event.type == "youtube.ended" && ctx.output != nullptr) {
                ctx.output->set_media_playing(false);
            }
        });
    }

    void on_chord(uint8_t, UiContext &ctx) override
    {
        if (state_ != YoutubeState::Results || results_.empty()) {
            return;
        }
        if (selected_ + 1 < results_.size()) {
            ++selected_;
            announce(ctx, std::to_string(selected_ + 1) + ". " + results_[selected_].title);
        }
    }

    void on_text(const std::string &text, UiContext &) override
    {
        if (state_ == YoutubeState::Search) {
            query_ += text;
        } else if (state_ == YoutubeState::Playing) {
            compose_.append(text);
        }
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed || ctx.connect == nullptr) {
            return;
        }

        if (key == keyboard::ControlKey::Enter) {
            if (state_ == YoutubeState::Search && !query_.empty()) {
                run_search(ctx);
            } else if (state_ == YoutubeState::Results && !results_.empty()) {
                play_selected(ctx);
            }
            return;
        }

        if (key == keyboard::ControlKey::Backspace) {
            if (state_ == YoutubeState::Playing) {
                ctx.connect->request("youtube.stop");
                if (ctx.output != nullptr) {
                    ctx.output->set_media_playing(false);
                }
                state_ = YoutubeState::Results;
                announce(ctx, "Playback stopped");
            } else if (state_ == YoutubeState::Search && !query_.empty()) {
                query_.pop_back();
            }
            return;
        }

        if (key == keyboard::ControlKey::DpadUp && state_ == YoutubeState::Results && selected_ > 0) {
            --selected_;
            announce(ctx, std::to_string(selected_ + 1) + ". " + results_[selected_].title);
        }
    }

private:
    void run_search(UiContext &ctx)
    {
        const std::string response = ctx.connect->request(
            "youtube.search",
            "\"query\":\"" + braillatron::connect::json_escape(query_) + "\"");
        results_.clear();
        selected_ = 0;

        const size_t arr = response.find("\"results\":[");
        if (arr == std::string::npos) {
            announce(ctx, "Search failed");
            return;
        }
        const size_t end = response.find(']', arr);
        const std::string array = response.substr(arr + 10, end - arr - 10);
        for (const auto &obj : braillatron::connect::json_split_objects("[" + array + "]")) {
            YoutubeResultItem item;
            item.title = braillatron::connect::json_get_string(obj, "title");
            item.url = braillatron::connect::json_get_string(obj, "url");
            if (!item.title.empty()) {
                results_.push_back(std::move(item));
            }
        }
        if (results_.empty()) {
            announce(ctx, "No results");
            return;
        }
        state_ = YoutubeState::Results;
        announce(ctx, "Found " + std::to_string(results_.size()) + " results");
        announce(ctx, "1. " + results_.front().title);
    }

    void play_selected(UiContext &ctx)
    {
        const std::string response = ctx.connect->request(
            "youtube.play", "\"url\":\"" + results_[selected_].url + "\"");
        if (braillatron::connect::json_get_bool(response, "ok", false)) {
            state_ = YoutubeState::Playing;
            if (ctx.output != nullptr) {
                ctx.output->set_media_playing(true);
            }
            announce(ctx, "Playing " + results_[selected_].title);
        } else {
            announce(ctx, "Playback failed");
        }
    }

    YoutubeState state_ = YoutubeState::Search;
    std::string query_;
    std::string compose_;
    std::vector<YoutubeResultItem> results_;
    size_t selected_ = 0;
};

} // namespace

std::unique_ptr<AppSession> make_youtube_app()
{
    return std::make_unique<YouTubeApp>();
}

} // namespace braillatron::ui
