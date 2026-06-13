#include "../../connect/connect_client.h"
#include "../../connect/json_utils.h"
#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../output_hub.h"

#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

struct FeedItem {
    std::string id;
    std::string title;
};

struct EpisodeItem {
    std::string id;
    std::string title;
    bool downloaded = false;
};

enum class Phase {
    Loading,
    Feeds,
    Episodes,
    Playing,
};

class PodcastsApp final : public AppSession {
public:
    std::string id() const override { return "podcasts"; }
    std::string label() const override { return "Podcasts"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        reset_session();
        if (ctx.connect == nullptr) {
            announce(ctx, "Connectivity unavailable.");
            return;
        }
        phase_ = Phase::Loading;
        announce(ctx, "Podcasts. Refreshing feeds.");
        ctx.connect->request("podcasts.import_opml");
        ctx.connect->request_async("podcasts.refresh", "", [this](const std::string &response) {
            if (braillatron::connect::json_get_bool(response, "ok", false)) {
                pending_load_feeds_ = true;
            } else {
                pending_announce_ = "Podcast refresh failed";
                phase_ = Phase::Feeds;
            }
        });
    }

    void on_exit(UiContext &ctx) override
    {
        if (ctx.connect != nullptr) {
            ctx.connect->request("podcasts.stop");
        }
        if (ctx.output != nullptr) {
            ctx.output->set_media_playing(false);
        }
        reset_session();
        announce(ctx, "Podcasts closed");
    }

    void on_poll(UiContext &ctx) override
    {
        if (pending_load_feeds_) {
            pending_load_feeds_ = false;
            load_feeds(ctx);
        }
        if (pending_start_playback_ && ctx.connect != nullptr) {
            pending_start_playback_ = false;
            const std::string response = ctx.connect->request(
                "podcasts.play", "\"episode_id\":\"" +
                                     braillatron::connect::json_escape(pending_play_episode_id_) +
                                     "\"");
            if (braillatron::connect::json_get_bool(response, "ok", false)) {
                phase_ = Phase::Playing;
                if (ctx.output != nullptr) {
                    ctx.output->set_media_playing(true);
                }
                pending_announce_ = "Playing " + pending_play_episode_title_;
            } else {
                pending_announce_ = "Playback failed";
            }
            pending_play_episode_id_.clear();
            pending_play_episode_title_.clear();
        }
        if (!pending_announce_.empty()) {
            announce(ctx, pending_announce_);
            pending_announce_.clear();
        }
    }

    void on_connect_event(const braillatron::connect::ConnectEvent &event, UiContext &ctx) override
    {
        if (event.type == "podcasts.ended") {
            phase_ = Phase::Episodes;
            if (ctx.output != nullptr) {
                ctx.output->set_media_playing(false);
            }
        }
        if (event.type == "podcasts.playing" && ctx.output != nullptr) {
            ctx.output->set_media_playing(true);
        }
    }

    void on_chord(uint8_t, UiContext &) override {}
    void on_text(const std::string &, UiContext &) override {}

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed || phase_ == Phase::Loading) {
            return;
        }

        switch (phase_) {
        case Phase::Feeds:
            handle_feeds(key, ctx);
            break;
        case Phase::Episodes:
            handle_episodes(key, ctx);
            break;
        case Phase::Playing:
            handle_playing(key, ctx);
            break;
        default:
            break;
        }
    }

private:
    void reset_session()
    {
        phase_ = Phase::Loading;
        feeds_.clear();
        episodes_.clear();
        feed_index_ = 0;
        episode_index_ = 0;
        pending_announce_.clear();
        pending_load_feeds_ = false;
        pending_start_playback_ = false;
        pending_play_episode_id_.clear();
        pending_play_episode_title_.clear();
        current_feed_title_.clear();
    }

    void load_feeds(UiContext &ctx)
    {
        if (ctx.connect == nullptr) {
            return;
        }
        const std::string response = ctx.connect->request("podcasts.list_feeds");
        feeds_.clear();
        if (!braillatron::connect::json_get_bool(response, "ok", false)) {
            pending_announce_ = "No podcast feeds. Import OPML via LocalSend.";
            phase_ = Phase::Feeds;
            return;
        }

        const size_t feeds_pos = response.find("\"feeds\":[");
        if (feeds_pos == std::string::npos) {
            pending_announce_ = "No podcast feeds. Import OPML via LocalSend.";
            phase_ = Phase::Feeds;
            return;
        }
        const size_t feeds_end = response.find(']', feeds_pos);
        const std::string feeds_array = response.substr(feeds_pos + 9, feeds_end - feeds_pos - 9);
        for (const auto &obj : braillatron::connect::json_split_objects("[" + feeds_array + "]")) {
            FeedItem feed;
            feed.id = braillatron::connect::json_get_string(obj, "id");
            feed.title = braillatron::connect::json_get_string(obj, "title");
            if (!feed.id.empty() && !feed.title.empty()) {
                feeds_.push_back(std::move(feed));
            }
        }

        if (feeds_.empty()) {
            pending_announce_ = "No podcast feeds. Import OPML via LocalSend.";
            phase_ = Phase::Feeds;
            return;
        }

        phase_ = Phase::Feeds;
        feed_index_ = 0;
        pending_announce_ = "Found " + std::to_string(feeds_.size()) + " subscriptions. 1. " +
                              feeds_.front().title;
    }

    void load_episodes(UiContext &ctx, const FeedItem &feed)
    {
        if (ctx.connect == nullptr) {
            return;
        }
        const std::string response = ctx.connect->request(
            "podcasts.list_episodes", "\"feed_id\":\"" + braillatron::connect::json_escape(feed.id) + "\"");
        episodes_.clear();
        current_feed_title_ = feed.title;

        if (!braillatron::connect::json_get_bool(response, "ok", false)) {
            pending_announce_ = "Could not load episodes";
            return;
        }

        const size_t episodes_pos = response.find("\"episodes\":[");
        if (episodes_pos == std::string::npos) {
            pending_announce_ = feed.title + ". No episodes";
            phase_ = Phase::Episodes;
            return;
        }
        const size_t episodes_end = response.find(']', episodes_pos);
        const std::string episodes_array =
            response.substr(episodes_pos + 12, episodes_end - episodes_pos - 12);
        for (const auto &obj :
             braillatron::connect::json_split_objects("[" + episodes_array + "]")) {
            EpisodeItem episode;
            episode.id = braillatron::connect::json_get_string(obj, "id");
            episode.title = braillatron::connect::json_get_string(obj, "title");
            episode.downloaded = braillatron::connect::json_get_bool(obj, "downloaded", false);
            if (!episode.id.empty() && !episode.title.empty()) {
                episodes_.push_back(std::move(episode));
            }
        }

        phase_ = Phase::Episodes;
        episode_index_ = 0;
        if (episodes_.empty()) {
            pending_announce_ = feed.title + ". No episodes";
        } else {
            pending_announce_ = feed.title + ". 1. " + episodes_.front().title;
        }
    }

    void handle_feeds(keyboard::ControlKey key, UiContext &ctx)
    {
        if (feeds_.empty()) {
            if (key == keyboard::ControlKey::Enter && ctx.connect != nullptr) {
                announce(ctx, "Refreshing feeds.");
                ctx.connect->request_async("podcasts.refresh", "", [this](const std::string &) {
                    pending_load_feeds_ = true;
                });
            }
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && feed_index_ > 0) {
            --feed_index_;
            announce(ctx, std::to_string(feed_index_ + 1) + ". " + feeds_[feed_index_].title);
        } else if (key == keyboard::ControlKey::DpadDown && feed_index_ + 1 < feeds_.size()) {
            ++feed_index_;
            announce(ctx, std::to_string(feed_index_ + 1) + ". " + feeds_[feed_index_].title);
        } else if (key == keyboard::ControlKey::Enter) {
            load_episodes(ctx, feeds_[feed_index_]);
        }
    }

    void handle_episodes(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Feeds;
            announce(ctx, std::to_string(feed_index_ + 1) + ". " + feeds_[feed_index_].title);
            return;
        }
        if (episodes_.empty()) {
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && episode_index_ > 0) {
            --episode_index_;
            announce(ctx, std::to_string(episode_index_ + 1) + ". " +
                               episodes_[episode_index_].title);
        } else if (key == keyboard::ControlKey::DpadDown && episode_index_ + 1 < episodes_.size()) {
            ++episode_index_;
            announce(ctx, std::to_string(episode_index_ + 1) + ". " +
                               episodes_[episode_index_].title);
        } else if (key == keyboard::ControlKey::Enter) {
            play_episode(ctx, episodes_[episode_index_]);
        }
    }

    void handle_playing(keyboard::ControlKey key, UiContext &ctx)
    {
        if (ctx.connect == nullptr) {
            return;
        }
        if (key == keyboard::ControlKey::Backspace) {
            ctx.connect->request("podcasts.stop");
            if (ctx.output != nullptr) {
                ctx.output->set_media_playing(false);
            }
            phase_ = Phase::Episodes;
            announce(ctx, "Playback stopped");
        }
    }

    void play_episode(UiContext &ctx, const EpisodeItem &episode)
    {
        if (ctx.connect == nullptr) {
            announce(ctx, "Connectivity unavailable");
            return;
        }

        if (!episode.downloaded) {
            announce(ctx, "Downloading " + episode.title);
            pending_play_episode_id_ = episode.id;
            pending_play_episode_title_ = episode.title;
            ctx.connect->request_async(
                "podcasts.download",
                "\"episode_id\":\"" + braillatron::connect::json_escape(episode.id) + "\"",
                [this](const std::string &response) {
                    if (!braillatron::connect::json_get_bool(response, "ok", false)) {
                        pending_announce_ = "Download failed";
                        pending_play_episode_id_.clear();
                        return;
                    }
                    pending_start_playback_ = true;
                });
            return;
        }
        start_playback(ctx, episode);
    }

    void start_playback(UiContext &ctx, const EpisodeItem &episode)
    {
        const std::string response = ctx.connect->request(
            "podcasts.play",
            "\"episode_id\":\"" + braillatron::connect::json_escape(episode.id) + "\"");
        if (braillatron::connect::json_get_bool(response, "ok", false)) {
            phase_ = Phase::Playing;
            if (ctx.output != nullptr) {
                ctx.output->set_media_playing(true);
            }
            announce(ctx, "Playing " + episode.title);
        } else {
            announce(ctx, "Playback failed");
        }
    }

    Phase phase_ = Phase::Loading;
    std::vector<FeedItem> feeds_;
    std::vector<EpisodeItem> episodes_;
    size_t feed_index_ = 0;
    size_t episode_index_ = 0;
    std::string pending_announce_;
    std::string current_feed_title_;
    bool pending_load_feeds_ = false;
    bool pending_start_playback_ = false;
    std::string pending_play_episode_id_;
    std::string pending_play_episode_title_;
};

} // namespace

std::unique_ptr<AppSession> make_podcasts_app()
{
    return std::make_unique<PodcastsApp>();
}

} // namespace braillatron::ui
