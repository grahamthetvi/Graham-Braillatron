#include "../../connect/connect_client.h"
#include "../../connect/json_utils.h"
#include "../layered_browse_list.h"
#include "app_registry.h"
#include "app_session.h"
#include "app_util.h"
#include "held_audio_skip.h"
#include "ui_context.h"

#include "../output_hub.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

constexpr size_t kMaxBrowseLabelLen = 72;
constexpr size_t kMaxAnnounceLen = 300;

uint64_t steady_now_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::string truncate_for_browse(const std::string &text)
{
    if (text.size() <= kMaxBrowseLabelLen) {
        return text;
    }
    return text.substr(0, kMaxBrowseLabelLen - 3) + "...";
}

std::string truncate_for_tts(const std::string &text)
{
    if (text.size() <= kMaxAnnounceLen) {
        return text;
    }
    return text.substr(0, kMaxAnnounceLen) + "...";
}

struct VideoItem {
    std::string title;
    std::string url;
    std::string channel;
    std::string duration;
};

enum class Phase {
    Menu,
    Loading,
    Search,
    Results,
    Playing,
};

enum class MenuChoice {
    Recommended,
    Search,
    Shorts,
};

enum class ResultsSource {
    Recommended,
    Search,
    Shorts,
};

class YouTubeApp final : public AppSession {
public:
    std::string id() const override { return "youtube"; }
    std::string label() const override { return "YouTube"; }
    AppKind kind() const override { return AppKind::Standalone; }

    bool browse_list_active() const override
    {
        return phase_ == Phase::Menu || phase_ == Phase::Results;
    }

    const LayeredBrowseList *browse_list() const override
    {
        return browse_list_active() ? &browse_ : nullptr;
    }

    std::string composer_line() const override
    {
        return phase_ == Phase::Search ? query_buffer_ : std::string {};
    }

    std::string browse_breadcrumb() const override { return breadcrumb_; }

    void on_enter(UiContext &ctx) override
    {
        reset_session();
        cookies_ready_ = false;
        if (ctx.connect != nullptr) {
            const std::string status = ctx.connect->request("accounts.status");
            cookies_ready_ =
                braillatron::connect::json_get_bool(status, "youtube_cookies", false);
        }
        rebuild_browse();
        sync_chrome(ctx);
        if (ctx.connect == nullptr) {
            announce(ctx, "Connectivity unavailable.");
            return;
        }
        if (cookies_ready_) {
            announce(ctx, "YouTube ready. Recommended, Search, or Shorts.");
        } else {
            announce(ctx,
                     "YouTube. Sign in optional. Copy cookies to credentials incoming folder for "
                     "personalized recommendations.");
        }
        browse_.announce_focus(ctx.output, false);
    }

    void on_exit(UiContext &ctx) override
    {
        if (ctx.registry != nullptr) {
            ctx.registry->clear_busy();
        }
        // Keep shared mpv playing so quick-settings controls work after leave.
        reset_session();
        announce(ctx, "YouTube closed");
    }

    void on_poll(UiContext &ctx) override
    {
        if (phase_ == Phase::Playing && ctx.connect != nullptr) {
            held_skip_.poll(steady_now_ms(), ctx.connect);
        }
        if (!pending_announce_.empty()) {
            announce(ctx, pending_announce_);
            pending_announce_.clear();
            sync_chrome(ctx);
            if (phase_ == Phase::Results && !results_.empty()) {
                announce_result(ctx);
            }
        }
    }

    void on_connect_event(const braillatron::connect::ConnectEvent &event, UiContext &ctx) override
    {
        if (event.type == "youtube.ended") {
            phase_ = Phase::Results;
            rebuild_browse();
            sync_chrome(ctx);
            if (ctx.output != nullptr) {
                ctx.output->set_media_playing(false);
            }
        }
        if (event.type == "youtube.playing" && ctx.output != nullptr) {
            ctx.output->set_media_playing(true);
            ctx.output->set_media_paused(false);
        }
    }

    void on_chord(uint8_t dot_mask, UiContext &) override
    {
        if (phase_ == Phase::Playing && is_skip_chord(dot_mask)) {
            return;
        }
    }

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
        case Phase::Menu:
            handle_menu(key, ctx);
            break;
        case Phase::Loading:
            if (key == keyboard::ControlKey::Backspace) {
                enter_menu(ctx);
            }
            break;
        case Phase::Search:
            handle_search(key, ctx);
            break;
        case Phase::Results:
            handle_results(key, ctx);
            break;
        case Phase::Playing:
            handle_playing(key, ctx);
            break;
        }
    }

private:
    void reset_session()
    {
        phase_ = Phase::Menu;
        results_.clear();
        query_buffer_.clear();
        breadcrumb_.clear();
        pending_announce_.clear();
        personalized_feed_ = false;
        browse_.clear();
    }

    void enter_menu(UiContext &ctx)
    {
        if (ctx.registry != nullptr) {
            ctx.registry->clear_busy();
        }
        phase_ = Phase::Menu;
        results_.clear();
        query_buffer_.clear();
        breadcrumb_.clear();
        rebuild_browse();
        sync_chrome(ctx);
        browse_.announce_focus(ctx.output, false);
    }

    void rebuild_browse()
    {
        std::vector<std::string> items;
        switch (phase_) {
        case Phase::Menu:
            breadcrumb_.clear();
            items = {"Recommended", "Search", "Shorts"};
            browse_.set_items(std::move(items), 0);
            browse_.set_container_name("YouTube");
            break;
        case Phase::Results:
            items.reserve(results_.size());
            for (const VideoItem &item : results_) {
                items.push_back(format_video_label(item));
            }
            browse_.set_items(std::move(items), 0);
            browse_.set_container_name(breadcrumb_);
            break;
        default:
            browse_.clear();
            break;
        }
    }

    static std::string format_video_label(const VideoItem &item)
    {
        std::string label = truncate_for_browse(item.title);
        if (!item.channel.empty() || !item.duration.empty()) {
            label += " —";
            if (!item.channel.empty()) {
                label += " " + truncate_for_browse(item.channel);
            }
            if (!item.duration.empty()) {
                label += " (" + item.duration + ")";
            }
        }
        return label;
    }

    void handle_menu(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (ctx.registry != nullptr) {
                ctx.registry->exit();
            }
            return;
        }
        if (key == keyboard::ControlKey::DpadUp || key == keyboard::ControlKey::DpadDown) {
            browse_.handle_control(key, ctx.output);
            return;
        }
        if (key != keyboard::ControlKey::Enter || ctx.connect == nullptr) {
            return;
        }

        switch (static_cast<MenuChoice>(browse_.focus_index())) {
        case MenuChoice::Recommended:
            load_feed(ctx, "youtube.recommended", ResultsSource::Recommended, "Recommended");
            break;
        case MenuChoice::Search:
            phase_ = Phase::Search;
            query_buffer_.clear();
            breadcrumb_ = "Search";
            rebuild_browse();
            sync_chrome(ctx);
            announce(ctx, "Search. Type a query and press Enter.");
            break;
        case MenuChoice::Shorts:
            load_feed(ctx, "youtube.shorts", ResultsSource::Shorts, "Shorts");
            break;
        }
    }

    void load_feed(UiContext &ctx, const std::string &command, ResultsSource source,
                   const std::string &label)
    {
        phase_ = Phase::Loading;
        results_source_ = source;
        breadcrumb_ = label;
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, "Loading " + label + ".");

        AppRegistry *registry = ctx.registry;
        if (registry != nullptr) {
            registry->mark_busy(steady_now_ms());
        }

        ctx.connect->request_async(command, "", [this, registry, label](const std::string &response) {
            parse_results_response(response, label);
            if (registry != nullptr) {
                registry->clear_busy();
            }
        });
    }

    void handle_search(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!query_buffer_.empty()) {
                query_buffer_.pop_back();
                sync_chrome(ctx);
                return;
            }
            enter_menu(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }
        if (query_buffer_.empty()) {
            announce(ctx, "Type a search query first.");
            return;
        }
        if (ctx.connect == nullptr) {
            announce(ctx, "Connectivity unavailable.");
            return;
        }

        phase_ = Phase::Loading;
        results_source_ = ResultsSource::Search;
        breadcrumb_ = "Search results";
        results_.clear();
        rebuild_browse();
        sync_chrome(ctx);
        announce(ctx, "Searching.");

        AppRegistry *registry = ctx.registry;
        if (registry != nullptr) {
            registry->mark_busy(steady_now_ms());
        }

        const std::string query = query_buffer_;
        const std::string payload =
            "\"query\":\"" + braillatron::connect::json_escape(query) + "\"";
        ctx.connect->request_async("youtube.search", payload,
                                   [this, registry](const std::string &response) {
                                       parse_results_response(response, "Search results");
                                       if (registry != nullptr) {
                                           registry->clear_busy();
                                       }
                                   });
    }

    void handle_results(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (results_source_ == ResultsSource::Search) {
                phase_ = Phase::Search;
                results_.clear();
                breadcrumb_ = "Search";
                rebuild_browse();
                sync_chrome(ctx);
                announce(ctx, "Search. " + query_buffer_);
            } else {
                enter_menu(ctx);
            }
            return;
        }
        if (results_.empty()) {
            return;
        }
        if (key == keyboard::ControlKey::DpadUp || key == keyboard::ControlKey::DpadDown) {
            browse_.handle_control(key, ctx.output);
            announce_result(ctx);
            return;
        }
        if (key != keyboard::ControlKey::Enter || ctx.connect == nullptr) {
            return;
        }
        play_selected(ctx, browse_.focus_index());
    }

    void handle_playing(keyboard::ControlKey key, UiContext &ctx)
    {
        if (ctx.connect == nullptr) {
            return;
        }
        if (key == keyboard::ControlKey::Backspace) {
            held_skip_.reset();
            ctx.connect->request("youtube.stop");
            if (ctx.output != nullptr) {
                ctx.output->set_media_playing(false);
            }
            phase_ = Phase::Results;
            rebuild_browse();
            sync_chrome(ctx);
            announce(ctx, "Playback stopped.");
            if (!results_.empty()) {
                announce_result(ctx);
            }
            return;
        }
        if (key == keyboard::ControlKey::Enter) {
            const std::string response = ctx.connect->request("youtube.pause");
            if (braillatron::connect::json_get_bool(response, "ok", false)) {
                const bool paused =
                    braillatron::connect::json_get_bool(response, "paused", false);
                if (ctx.output != nullptr) {
                    ctx.output->set_media_paused(paused);
                }
                announce(ctx, paused ? "Paused" : "Playing");
            }
        }
    }

    void parse_results_response(const std::string &response, const std::string &label)
    {
        results_.clear();
        personalized_feed_ =
            braillatron::connect::json_get_bool(response, "personalized", false);

        if (!braillatron::connect::json_get_bool(response, "ok", false)) {
            const std::string error = braillatron::connect::json_get_string(response, "error");
            if (error == "no results" && results_source_ == ResultsSource::Recommended &&
                cookies_ready_) {
                pending_announce_ =
                    "No personalized recommendations. Add fresh YouTube cookies or try Search.";
            } else if (error == "no results") {
                pending_announce_ = "No videos found.";
            } else if (error.empty()) {
                pending_announce_ = label + " failed.";
            } else {
                pending_announce_ = label + " failed: " + error + ".";
            }
            phase_ = results_source_ == ResultsSource::Search ? Phase::Search : Phase::Menu;
            breadcrumb_.clear();
            rebuild_browse();
            return;
        }

        const size_t arr = response.find("\"results\":[");
        if (arr == std::string::npos) {
            pending_announce_ = label + " failed.";
            phase_ = results_source_ == ResultsSource::Search ? Phase::Search : Phase::Menu;
            breadcrumb_.clear();
            rebuild_browse();
            return;
        }

        const size_t end = response.find(']', arr);
        const std::string array = response.substr(arr + 10, end - arr - 10);
        for (const auto &obj : braillatron::connect::json_split_objects("[" + array + "]")) {
            VideoItem item;
            item.title = braillatron::connect::json_get_string(obj, "title");
            item.url = braillatron::connect::json_get_string(obj, "url");
            item.channel = braillatron::connect::json_get_string(obj, "channel");
            item.duration = braillatron::connect::json_get_string(obj, "duration");
            if (!item.title.empty() && !item.url.empty()) {
                results_.push_back(std::move(item));
            }
        }

        if (results_.empty()) {
            pending_announce_ = "No videos found.";
            phase_ = results_source_ == ResultsSource::Search ? Phase::Search : Phase::Menu;
            breadcrumb_.clear();
            rebuild_browse();
            return;
        }

        phase_ = Phase::Results;
        breadcrumb_ = label;
        rebuild_browse();

        std::string intro = "Found " + std::to_string(results_.size()) + " videos.";
        if (results_source_ == ResultsSource::Recommended) {
            intro += personalized_feed_ ? " Personalized feed." : " Popular videos.";
        }
        pending_announce_ = intro;
    }

    void play_selected(UiContext &ctx, size_t index)
    {
        if (index >= results_.size()) {
            return;
        }
        const VideoItem &item = results_[index];
        const std::string response = ctx.connect->request(
            "youtube.play", "\"url\":\"" + braillatron::connect::json_escape(item.url) + "\"");
        if (braillatron::connect::json_get_bool(response, "ok", false)) {
            phase_ = Phase::Playing;
            held_skip_.reset();
            if (ctx.output != nullptr) {
                ctx.output->set_media_playing(true);
                ctx.output->set_media_paused(false);
            }
            announce(ctx, "Playing " + truncate_for_tts(item.title) +
                               ". Enter pause. Hold dots 1-2-3 skip back, 4-5-6 skip forward. "
                               "Backspace stop.");
        } else {
            const std::string error = braillatron::connect::json_get_string(response, "error");
            if (error.empty()) {
                announce(ctx, "Playback failed.");
            } else {
                announce(ctx, "Playback failed: " + error + ".");
            }
        }
    }

    void announce_result(UiContext &ctx)
    {
        if (results_.empty()) {
            return;
        }
        const size_t index = browse_.focus_index();
        if (index >= results_.size()) {
            return;
        }
        const VideoItem &item = results_[index];
        std::string message = "Video " + std::to_string(index + 1) + " of " +
                              std::to_string(results_.size()) + ". " +
                              truncate_for_tts(item.title);
        if (!item.channel.empty()) {
            message += ". " + truncate_for_tts(item.channel);
        }
        if (!item.duration.empty()) {
            message += ". " + item.duration;
        }
        announce(ctx, message);
    }

    Phase phase_ = Phase::Menu;
    ResultsSource results_source_ = ResultsSource::Recommended;
    std::string query_buffer_;
    std::string breadcrumb_;
    std::vector<VideoItem> results_;
    LayeredBrowseList browse_;
    std::string pending_announce_;
    bool cookies_ready_ = false;
    bool personalized_feed_ = false;
    HeldAudioSkip held_skip_;
};

} // namespace

std::unique_ptr<AppSession> make_youtube_app()
{
    return std::make_unique<YouTubeApp>();
}

} // namespace braillatron::ui
