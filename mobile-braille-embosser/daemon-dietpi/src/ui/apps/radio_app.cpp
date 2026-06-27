#include "../../connect/connect_client.h"
#include "../../connect/json_utils.h"
#include "app_registry.h"
#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../output_hub.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

uint64_t steady_now_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

struct StationItem {
    std::string id;
    std::string name;
    std::string country;
    bool favorite = false;
};

enum class Phase {
    Loading,
    Stations,
    Starting,
    Playing,
    Searching,
};

class RadioApp final : public AppSession {
public:
    std::string id() const override { return "radio"; }
    std::string label() const override { return "Radio"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        reset_session();
        if (ctx.connect == nullptr) {
            announce(ctx, "Connectivity unavailable.");
            return;
        }
        phase_ = Phase::Loading;
        announce(ctx, "Internet Radio. Loading stations.");
        load_stations(ctx);
    }

    void on_exit(UiContext &ctx) override
    {
        if (ctx.registry != nullptr) {
            ctx.registry->clear_busy();
        }
        if (ctx.connect != nullptr) {
            ctx.connect->request("radio.stop");
        }
        if (ctx.output != nullptr) {
            ctx.output->set_media_playing(false);
        }
        reset_session();
        announce(ctx, "Radio closed");
    }

    void on_poll(UiContext &ctx) override
    {
        if (!pending_announce_.empty()) {
            announce(ctx, pending_announce_);
            pending_announce_.clear();
        }
    }

    void on_connect_event(const braillatron::connect::ConnectEvent &event, UiContext &ctx) override
    {
        if (event.type == "radio.ended") {
            phase_ = Phase::Stations;
            if (ctx.output != nullptr) {
                ctx.output->set_media_playing(false);
            }
        }
        if (event.type == "radio.playing" && ctx.output != nullptr) {
            ctx.output->set_media_playing(true);
        }
        if (event.type == "radio.metadata") {
            const std::string title = braillatron::connect::json_get_string(event.data_json, "title");
            if (!title.empty()) {
                pending_announce_ = "Now playing: " + title;
            }
        }
    }

    void on_chord(uint8_t, UiContext &) override {}
    void on_text(const std::string &, UiContext &) override {}

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed || phase_ == Phase::Loading || phase_ == Phase::Starting) {
            return;
        }

        switch (phase_) {
        case Phase::Stations:
            handle_stations(key, ctx);
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
        stations_.clear();
        station_index_ = 0;
        pending_announce_.clear();
    }

    void parse_stations_response(const std::string &response)
    {
        stations_.clear();
        if (!braillatron::connect::json_get_bool(response, "ok", false)) {
            pending_announce_ = "Could not load radio stations";
            phase_ = Phase::Stations;
            return;
        }

        const size_t stations_pos = response.find("\"stations\":[");
        if (stations_pos == std::string::npos) {
            pending_announce_ = "No radio stations found";
            phase_ = Phase::Stations;
            return;
        }
        const size_t stations_end = response.find(']', stations_pos);
        const std::string stations_array =
            response.substr(stations_pos + 12, stations_end - stations_pos - 12);
        for (const auto &obj :
             braillatron::connect::json_split_objects("[" + stations_array + "]")) {
            StationItem station;
            station.id = braillatron::connect::json_get_string(obj, "id");
            station.name = braillatron::connect::json_get_string(obj, "name");
            station.country = braillatron::connect::json_get_string(obj, "country");
            station.favorite = braillatron::connect::json_get_bool(obj, "favorite", false);
            if (!station.id.empty() && !station.name.empty()) {
                stations_.push_back(std::move(station));
            }
        }

        phase_ = Phase::Stations;
        station_index_ = 0;
        if (stations_.empty()) {
            pending_announce_ = "No radio stations found";
        } else {
            pending_announce_ = std::to_string(stations_.size()) + " stations. 1. " +
                                  stations_.front().name;
        }
    }

    void load_stations(UiContext &ctx)
    {
        if (ctx.connect == nullptr) {
            return;
        }
        AppRegistry *registry = ctx.registry;
        if (registry != nullptr) {
            registry->mark_busy(steady_now_ms());
        }
        ctx.connect->request_async("radio.list_stations", "", [this, registry](const std::string &response) {
            parse_stations_response(response);
            if (registry != nullptr) {
                registry->clear_busy();
            }
        });
    }

    void handle_stations(keyboard::ControlKey key, UiContext &ctx)
    {
        if (stations_.empty()) {
            return;
        }
        if (key == keyboard::ControlKey::DpadUp && station_index_ > 0) {
            --station_index_;
            announce(ctx, std::to_string(station_index_ + 1) + ". " +
                               stations_[station_index_].name);
        } else if (key == keyboard::ControlKey::DpadDown && station_index_ + 1 < stations_.size()) {
            ++station_index_;
            announce(ctx, std::to_string(station_index_ + 1) + ". " +
                               stations_[station_index_].name);
        } else if (key == keyboard::ControlKey::Enter) {
            play_station(ctx, stations_[station_index_]);
        }
    }

    void handle_playing(keyboard::ControlKey key, UiContext &ctx)
    {
        if (ctx.connect == nullptr) {
            return;
        }
        if (key == keyboard::ControlKey::Backspace) {
            ctx.connect->request("radio.stop");
            if (ctx.output != nullptr) {
                ctx.output->set_media_playing(false);
            }
            phase_ = Phase::Stations;
            announce(ctx, "Playback stopped");
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            ctx.connect->request(
                "radio.favorites.add",
                "\"station_id\":\"" +
                    braillatron::connect::json_escape(stations_[station_index_].id) + "\"");
            announce(ctx, "Added to favorites");
        }
    }

    void play_station(UiContext &ctx, const StationItem &station)
    {
        if (ctx.connect == nullptr) {
            announce(ctx, "Connectivity unavailable");
            return;
        }
        phase_ = Phase::Starting;
        announce(ctx, "Starting " + station.name);
        AppRegistry *registry = ctx.registry;
        if (registry != nullptr) {
            registry->mark_busy(steady_now_ms());
        }
        ctx.connect->request_async(
            "radio.play",
            "\"station_id\":\"" + braillatron::connect::json_escape(station.id) + "\"",
            [this, registry, station_name = station.name](const std::string &response) {
                if (braillatron::connect::json_get_bool(response, "ok", false)) {
                    phase_ = Phase::Playing;
                    pending_announce_ = "Playing " + station_name;
                } else {
                    phase_ = Phase::Stations;
                    pending_announce_ = "Playback failed";
                }
                if (registry != nullptr) {
                    registry->clear_busy();
                }
            });
    }

    Phase phase_ = Phase::Loading;
    std::vector<StationItem> stations_;
    size_t station_index_ = 0;
    std::string pending_announce_;
};

} // namespace

std::unique_ptr<AppSession> make_radio_app()
{
    return std::make_unique<RadioApp>();
}

} // namespace braillatron::ui
