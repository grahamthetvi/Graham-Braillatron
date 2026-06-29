#include "../../connect/connect_client.h"
#include "../../connect/json_utils.h"
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

struct MusicTrackItem {
    std::string id;
    std::string title;
};

struct MusicAlbumItem {
    std::string name;
    std::vector<MusicTrackItem> tracks;
};

struct MusicArtistItem {
    std::string name;
    std::vector<MusicAlbumItem> albums;
};

enum class Phase {
    Scanning,
    Artists,
    Albums,
    Tracks,
    Playing,
};

class MusicApp final : public AppSession {
public:
    std::string id() const override { return "music"; }
    std::string label() const override { return "Music"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        reset_session();
        if (ctx.connect == nullptr) {
            announce(ctx, "Connectivity unavailable.");
            return;
        }
        phase_ = Phase::Scanning;
        announce(ctx, "Music. Scanning library.");
        ctx.connect->request_async("music.scan", "", [this](const std::string &response) {
            parse_scan_response(response);
        });
    }

    void on_exit(UiContext &ctx) override
    {
        reset_session();
        announce(ctx, "Music closed");
    }

    void on_poll(UiContext &ctx) override
    {
        if (phase_ == Phase::Playing && ctx.connect != nullptr) {
            held_skip_.poll(now_ms(), ctx.connect);
        }
        if (!pending_announce_.empty()) {
            announce(ctx, pending_announce_);
            pending_announce_.clear();
        }
    }

    void on_connect_event(const braillatron::connect::ConnectEvent &event, UiContext &ctx) override
    {
        if (event.type == "music.ended") {
            phase_ = Phase::Tracks;
            if (ctx.output != nullptr) {
                ctx.output->set_media_playing(false);
            }
        }
        if (event.type == "music.playing" && ctx.output != nullptr) {
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
    void on_text(const std::string &, UiContext &) override {}

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed || phase_ == Phase::Scanning) {
            return;
        }

        switch (phase_) {
        case Phase::Artists:
            handle_artists(key, ctx);
            break;
        case Phase::Albums:
            handle_albums(key, ctx);
            break;
        case Phase::Tracks:
            handle_tracks(key, ctx);
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
        phase_ = Phase::Scanning;
        artists_.clear();
        artist_index_ = 0;
        album_index_ = 0;
        track_index_ = 0;
        pending_announce_.clear();
        now_playing_.clear();
        held_skip_.reset();
    }

    static uint64_t now_ms()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    void parse_scan_response(const std::string &response)
    {
        artists_.clear();
        if (!braillatron::connect::json_get_bool(response, "ok", false)) {
            pending_announce_ = "Music scan failed";
            return;
        }

        const size_t artists_pos = response.find("\"artists\":[");
        if (artists_pos == std::string::npos) {
            pending_announce_ = "No music found. Copy audio files to music folder.";
            return;
        }
        const size_t artists_end = response.find(']', artists_pos);
        const std::string artists_array =
            response.substr(artists_pos + 10, artists_end - artists_pos - 10);

        for (const auto &artist_obj :
             braillatron::connect::json_split_objects("[" + artists_array + "]")) {
            MusicArtistItem artist;
            artist.name = braillatron::connect::json_get_string(artist_obj, "name");
            const size_t albums_pos = artist_obj.find("\"albums\":[");
            if (albums_pos != std::string::npos) {
                const size_t albums_end = artist_obj.find(']', albums_pos);
                const std::string albums_array =
                    artist_obj.substr(albums_pos + 9, albums_end - albums_pos - 9);
                for (const auto &album_obj :
                     braillatron::connect::json_split_objects("[" + albums_array + "]")) {
                    MusicAlbumItem album;
                    album.name = braillatron::connect::json_get_string(album_obj, "name");
                    const size_t tracks_pos = album_obj.find("\"tracks\":[");
                    if (tracks_pos != std::string::npos) {
                        const size_t tracks_end = album_obj.find(']', tracks_pos);
                        const std::string tracks_array =
                            album_obj.substr(tracks_pos + 9, tracks_end - tracks_pos - 9);
                        for (const auto &track_obj :
                             braillatron::connect::json_split_objects("[" + tracks_array + "]")) {
                            MusicTrackItem track;
                            track.id = braillatron::connect::json_get_string(track_obj, "id");
                            track.title = braillatron::connect::json_get_string(track_obj, "title");
                            if (!track.id.empty() && !track.title.empty()) {
                                album.tracks.push_back(std::move(track));
                            }
                        }
                    }
                    if (!album.tracks.empty()) {
                        artist.albums.push_back(std::move(album));
                    }
                }
            }
            if (!artist.albums.empty()) {
                artists_.push_back(std::move(artist));
            }
        }

        if (artists_.empty()) {
            pending_announce_ = "No music found. Copy audio files to music folder.";
            return;
        }

        phase_ = Phase::Artists;
        artist_index_ = 0;
        pending_announce_ = "Found music. " + std::to_string(artists_.size()) + " artists. 1. " +
                              artists_.front().name;
    }

    void handle_artists(keyboard::ControlKey key, UiContext &ctx)
    {
        if (artists_.empty()) {
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && artist_index_ > 0) {
            --artist_index_;
            announce(ctx, std::to_string(artist_index_ + 1) + ". " + artists_[artist_index_].name);
        } else if (key == keyboard::ControlKey::DpadDown &&
                   artist_index_ + 1 < artists_.size()) {
            ++artist_index_;
            announce(ctx, std::to_string(artist_index_ + 1) + ". " + artists_[artist_index_].name);
        } else if (key == keyboard::ControlKey::Enter) {
            album_index_ = 0;
            phase_ = Phase::Albums;
            announce(ctx, artists_[artist_index_].name + ". 1. " +
                               artists_[artist_index_].albums.front().name);
        }
    }

    void handle_albums(keyboard::ControlKey key, UiContext &ctx)
    {
        const auto &albums = artists_[artist_index_].albums;
        if (albums.empty()) {
            return;
        }
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Artists;
            announce(ctx, std::to_string(artist_index_ + 1) + ". " + artists_[artist_index_].name);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && album_index_ > 0) {
            --album_index_;
            announce(ctx, std::to_string(album_index_ + 1) + ". " + albums[album_index_].name);
        } else if (key == keyboard::ControlKey::DpadDown && album_index_ + 1 < albums.size()) {
            ++album_index_;
            announce(ctx, std::to_string(album_index_ + 1) + ". " + albums[album_index_].name);
        } else if (key == keyboard::ControlKey::Enter) {
            track_index_ = 0;
            phase_ = Phase::Tracks;
            announce(ctx, albums[album_index_].name + ". 1. " +
                               albums[album_index_].tracks.front().title);
        }
    }

    void handle_tracks(keyboard::ControlKey key, UiContext &ctx)
    {
        const auto &tracks = artists_[artist_index_].albums[album_index_].tracks;
        if (tracks.empty()) {
            return;
        }
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Albums;
            announce(ctx, std::to_string(album_index_ + 1) + ". " +
                               artists_[artist_index_].albums[album_index_].name);
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && track_index_ > 0) {
            --track_index_;
            announce(ctx, std::to_string(track_index_ + 1) + ". " + tracks[track_index_].title);
        } else if (key == keyboard::ControlKey::DpadDown && track_index_ + 1 < tracks.size()) {
            ++track_index_;
            announce(ctx, std::to_string(track_index_ + 1) + ". " + tracks[track_index_].title);
        } else if (key == keyboard::ControlKey::Enter) {
            play_track(ctx, tracks[track_index_]);
        }
    }

    void handle_playing(keyboard::ControlKey key, UiContext &ctx)
    {
        if (ctx.connect == nullptr) {
            return;
        }
        if (key == keyboard::ControlKey::Backspace) {
            ctx.connect->request("music.stop");
            if (ctx.output != nullptr) {
                ctx.output->set_media_playing(false);
            }
            phase_ = Phase::Tracks;
            announce(ctx, "Playback stopped");
            return;
        }
        if (key == keyboard::ControlKey::Enter) {
            const std::string response = ctx.connect->request("music.pause");
            if (braillatron::connect::json_get_bool(response, "ok", false)) {
                const bool paused = braillatron::connect::json_get_bool(response, "paused", false);
                if (ctx.output != nullptr) {
                    ctx.output->set_media_paused(paused);
                }
                announce(ctx, paused ? "Paused" : "Playing " + now_playing_);
            }
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            const std::string response = ctx.connect->request("music.next");
            if (braillatron::connect::json_get_bool(response, "ok", false)) {
                now_playing_ = braillatron::connect::json_get_string(response, "title");
                if (now_playing_.empty()) {
                    const std::string track_json = response.substr(response.find("\"track\":"));
                    now_playing_ = braillatron::connect::json_get_string(track_json, "title");
                }
                if (ctx.output != nullptr) {
                    ctx.output->set_media_paused(false);
                }
                announce(ctx, "Playing " + now_playing_);
            }
        } else if (key == keyboard::ControlKey::DpadUp) {
            const std::string response = ctx.connect->request("music.prev");
            if (braillatron::connect::json_get_bool(response, "ok", false)) {
                now_playing_ = braillatron::connect::json_get_string(response, "title");
                if (now_playing_.empty()) {
                    const std::string track_json = response.substr(response.find("\"track\":"));
                    now_playing_ = braillatron::connect::json_get_string(track_json, "title");
                }
                if (ctx.output != nullptr) {
                    ctx.output->set_media_paused(false);
                }
                announce(ctx, "Playing " + now_playing_);
            }
        }
    }

    void play_track(UiContext &ctx, const MusicTrackItem &track)
    {
        if (ctx.connect == nullptr) {
            announce(ctx, "Connectivity unavailable");
            return;
        }
        const std::string response = ctx.connect->request(
            "music.play", "\"track_id\":\"" + braillatron::connect::json_escape(track.id) + "\"");
        if (braillatron::connect::json_get_bool(response, "ok", false)) {
            phase_ = Phase::Playing;
            now_playing_ = track.title;
            if (ctx.output != nullptr) {
                ctx.output->set_media_playing(true);
                ctx.output->set_media_paused(false);
            }
            announce(ctx, "Playing " + track.title +
                               ". Enter pause. Hold dots 1-2-3 skip back, 4-5-6 skip forward. "
                               "Up previous track. Down next track. Backspace stop.");
        } else {
            announce(ctx, "Playback failed");
        }
    }

    Phase phase_ = Phase::Scanning;
    std::vector<MusicArtistItem> artists_;
    size_t artist_index_ = 0;
    size_t album_index_ = 0;
    size_t track_index_ = 0;
    std::string pending_announce_;
    std::string now_playing_;
    HeldAudioSkip held_skip_;
};

} // namespace

std::unique_ptr<AppSession> make_music_app()
{
    return std::make_unique<MusicApp>();
}

} // namespace braillatron::ui
