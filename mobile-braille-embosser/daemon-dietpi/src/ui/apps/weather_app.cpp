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

struct HourlyItem {
    std::string label;
    std::string description;
    std::string temperature;
    std::string humidity;
    std::string uv_index;
    std::string precip_probability;
};

struct DailyItem {
    std::string label;
    std::string description;
    std::string temp_max;
    std::string temp_min;
    std::string precip_probability;
    std::string uv_index;
};

enum class Phase {
    Loading,
    Menu,
    Current,
    Hourly,
    Daily,
    Location,
};

class WeatherApp final : public AppSession {
public:
    std::string id() const override { return "weather"; }
    std::string label() const override { return "Weather"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        reset_session();
        if (ctx.connect == nullptr) {
            announce(ctx, "Connectivity unavailable.");
            return;
        }

        announce(ctx, "Weather loading.");
        const std::string read_response = ctx.connect->request("weather.read");
        if (braillatron::connect::json_get_bool(read_response, "ok", false)) {
            const bool fresh = braillatron::connect::json_get_bool(read_response, "fresh", false);
            parse_cache_response(read_response, !fresh);
            phase_ = Phase::Menu;
            menu_index_ = 0;
            pending_announce_ =
                build_ready_announcement(fresh ? "" : "Showing cached forecast");
            start_background_fetch(ctx);
            return;
        }

        phase_ = Phase::Loading;
        announce(ctx, "Fetching forecast.");
        start_fetch(ctx, false);
    }

    void on_exit(UiContext &ctx) override
    {
        reset_session();
        announce(ctx, "Weather closed");
    }

    void on_poll(UiContext &ctx) override
    {
        if (!pending_announce_.empty()) {
            announce(ctx, pending_announce_);
            pending_announce_.clear();
        }
    }

    void on_chord(uint8_t, UiContext &) override {}

    bool buffers_braille_words() const override { return phase_ == Phase::Location; }

    void on_text(const std::string &text, UiContext &) override
    {
        if (phase_ != Phase::Location || text.empty()) {
            return;
        }
        city_buffer_ += text;
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed || phase_ == Phase::Loading) {
            return;
        }

        switch (phase_) {
        case Phase::Menu:
            handle_menu(key, ctx);
            break;
        case Phase::Current:
            handle_current(key, ctx);
            break;
        case Phase::Hourly:
            handle_hourly(key, ctx);
            break;
        case Phase::Daily:
            handle_daily(key, ctx);
            break;
        case Phase::Location:
            handle_location(key, ctx);
            break;
        default:
            break;
        }
    }

    void on_connect_event(const braillatron::connect::ConnectEvent &event, UiContext &ctx) override
    {
        if (event.type == "weather.updated" && background_fetch_pending_) {
            background_fetch_pending_ = false;
            pending_announce_ = "Forecast updated";
        }
        (void)ctx;
    }

private:
    void reset_session()
    {
        phase_ = Phase::Loading;
        location_.clear();
        current_summary_.clear();
        hourly_.clear();
        daily_.clear();
        menu_index_ = 0;
        list_index_ = 0;
        pending_announce_.clear();
        temperature_unit_ = "celsius";
        city_buffer_.clear();
        background_fetch_pending_ = false;
    }

    void start_fetch(UiContext &ctx, bool background)
    {
        background_fetch_pending_ = background;
        if (!background) {
            phase_ = Phase::Loading;
        }
        ctx.connect->request_async("weather.fetch", "", [this, background](const std::string &response) {
            if (!braillatron::connect::json_get_bool(response, "ok", false)) {
                if (!background) {
                    pending_announce_ = "Weather fetch failed";
                    phase_ = Phase::Menu;
                }
                background_fetch_pending_ = false;
                return;
            }
            const bool stale = braillatron::connect::json_get_bool(response, "stale", false);
            parse_cache_response(response, stale);
            phase_ = Phase::Menu;
            menu_index_ = 0;
            if (background) {
                pending_announce_ = "Forecast updated";
            } else {
                pending_announce_ = build_ready_announcement(stale ? "Showing cached forecast" : "");
            }
            background_fetch_pending_ = false;
        });
    }

    void start_background_fetch(UiContext &ctx) { start_fetch(ctx, true); }

    std::string build_ready_announcement(const std::string &suffix) const
    {
        std::string message =
            location_.empty() ? "Weather ready" : "Weather for " + location_;
        if (!suffix.empty()) {
            message += ". " + suffix;
        }
        message += ". Current, Hourly, Daily, Location, or Refresh. Press Enter.";
        return message;
    }

    void parse_cache_response(const std::string &response, bool stale)
    {
        (void)stale;
        const size_t cache_pos = response.find("\"cache\":{");
        if (cache_pos == std::string::npos) {
            pending_announce_ = "Weather data unavailable";
            phase_ = Phase::Menu;
            return;
        }

        const std::string cache = response.substr(cache_pos + 8);
        location_ = braillatron::connect::json_get_string(cache, "location");
        temperature_unit_ = braillatron::connect::json_get_string(cache, "temperature_unit");
        if (temperature_unit_.empty()) {
            temperature_unit_ = "celsius";
        }

        const size_t current_key = cache.find("\"current\"");
        if (current_key != std::string::npos) {
            const size_t current_pos = cache.find('{', current_key);
            if (current_pos != std::string::npos) {
                const size_t current_end = cache.find('}', current_pos);
                const std::string current_block =
                    cache.substr(current_pos, current_end - current_pos + 1);
                const std::string temp =
                    braillatron::connect::json_get_string(current_block, "temperature");
                const std::string desc =
                    braillatron::connect::json_get_string(current_block, "weather_description");
                const std::string wind =
                    braillatron::connect::json_get_string(current_block, "wind_speed");
                const std::string humidity =
                    braillatron::connect::json_get_string(current_block, "relative_humidity");
                const std::string uv =
                    braillatron::connect::json_get_string(current_block, "uv_index");
                const std::string precip =
                    braillatron::connect::json_get_string(current_block, "precipitation_probability");

                current_summary_ = format_temperature(temp) + ". " + desc;
                if (!humidity.empty() && humidity != "0") {
                    current_summary_ += ". Humidity " + humidity + " percent";
                }
                if (!uv.empty() && uv != "0") {
                    current_summary_ += ". UV index " + uv;
                }
                if (!precip.empty() && precip != "0") {
                    current_summary_ += ". Precipitation chance " + precip + " percent";
                }
                if (!wind.empty() && wind != "0") {
                    current_summary_ += ". Wind " + wind;
                }
            }
        }

        hourly_.clear();
        const size_t hourly_pos = cache.find("\"hourly\"");
        if (hourly_pos != std::string::npos) {
            const size_t array_start = cache.find('[', hourly_pos);
            const size_t hourly_end = cache.find(']', array_start);
            const std::string hourly_array =
                cache.substr(array_start + 1, hourly_end - array_start - 1);
            for (const auto &item :
                 braillatron::connect::json_split_objects("[" + hourly_array + "]")) {
                HourlyItem entry;
                entry.label = braillatron::connect::json_get_string(item, "label");
                entry.description =
                    braillatron::connect::json_get_string(item, "weather_description");
                entry.temperature = braillatron::connect::json_get_string(item, "temperature");
                entry.humidity =
                    braillatron::connect::json_get_string(item, "relative_humidity");
                entry.uv_index = braillatron::connect::json_get_string(item, "uv_index");
                entry.precip_probability =
                    braillatron::connect::json_get_string(item, "precipitation_probability");
                if (!entry.label.empty()) {
                    hourly_.push_back(entry);
                }
            }
        }

        daily_.clear();
        const size_t daily_pos = cache.find("\"daily\"");
        if (daily_pos != std::string::npos) {
            const size_t array_start = cache.find('[', daily_pos);
            const size_t daily_end = cache.find(']', array_start);
            const std::string daily_array = cache.substr(array_start + 1, daily_end - array_start - 1);
            for (const auto &item :
                 braillatron::connect::json_split_objects("[" + daily_array + "]")) {
                DailyItem entry;
                entry.label = braillatron::connect::json_get_string(item, "label");
                entry.description =
                    braillatron::connect::json_get_string(item, "weather_description");
                entry.temp_max = braillatron::connect::json_get_string(item, "temp_max");
                entry.temp_min = braillatron::connect::json_get_string(item, "temp_min");
                entry.precip_probability =
                    braillatron::connect::json_get_string(item, "precipitation_probability_max");
                entry.uv_index = braillatron::connect::json_get_string(item, "uv_index_max");
                if (!entry.label.empty()) {
                    daily_.push_back(entry);
                }
            }
        }
    }

    std::string format_temperature(const std::string &value) const
    {
        if (value.empty()) {
            return "Unknown temperature";
        }
        if (temperature_unit_ == "fahrenheit") {
            return value + " degrees Fahrenheit";
        }
        return value + " degrees Celsius";
    }

    void handle_menu(keyboard::ControlKey key, UiContext &ctx)
    {
        static const std::vector<std::string> kMenuItems = {"Current", "Hourly", "Daily",
                                                            "Location", "Refresh"};

        if (key == keyboard::ControlKey::DpadUp) {
            if (menu_index_ > 0) {
                --menu_index_;
                announce(ctx, kMenuItems[menu_index_]);
            }
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            if (menu_index_ + 1 < kMenuItems.size()) {
                ++menu_index_;
                announce(ctx, kMenuItems[menu_index_]);
            }
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        if (menu_index_ == 0) {
            phase_ = Phase::Current;
            announce(ctx, current_summary_.empty() ? "No current conditions" : current_summary_);
            return;
        }
        if (menu_index_ == 1) {
            if (hourly_.empty()) {
                announce(ctx, "No hourly forecast");
                return;
            }
            phase_ = Phase::Hourly;
            list_index_ = 0;
            announce_hourly(ctx);
            return;
        }
        if (menu_index_ == 2) {
            if (daily_.empty()) {
                announce(ctx, "No daily forecast");
                return;
            }
            phase_ = Phase::Daily;
            list_index_ = 0;
            announce_daily(ctx);
            return;
        }
        if (menu_index_ == 3) {
            phase_ = Phase::Location;
            city_buffer_ = location_;
            announce(ctx, "Enter city name and press Enter. Current location " +
                               (location_.empty() ? "not set" : location_));
            return;
        }
        if (menu_index_ == 4) {
            announce(ctx, "Refreshing forecast");
            start_fetch(ctx, false);
        }
    }

    void handle_location(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!city_buffer_.empty()) {
                city_buffer_.pop_back();
            } else {
                phase_ = Phase::Menu;
                announce(ctx, "Weather menu. Location selected");
            }
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }
        if (city_buffer_.empty()) {
            announce(ctx, "Type a city name first");
            return;
        }

        phase_ = Phase::Loading;
        announce(ctx, "Updating location to " + city_buffer_);
        const std::string fields =
            "\"city_name\":\"" + braillatron::connect::json_escape(city_buffer_) + "\"";
        ctx.connect->request_async("weather.set_location", fields, [this](const std::string &response) {
            if (!braillatron::connect::json_get_bool(response, "ok", false)) {
                pending_announce_ = "Location update failed";
                phase_ = Phase::Menu;
                menu_index_ = 3;
                return;
            }
            parse_cache_response(response, false);
            phase_ = Phase::Menu;
            menu_index_ = 3;
            pending_announce_ = build_ready_announcement("");
        });
    }

    void handle_current(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Menu;
            announce(ctx, "Weather menu. Current selected");
        }
    }

    void handle_hourly(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Menu;
            announce(ctx, "Weather menu. Hourly selected");
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            if (list_index_ > 0) {
                --list_index_;
                announce_hourly(ctx);
            }
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            if (list_index_ + 1 < hourly_.size()) {
                ++list_index_;
                announce_hourly(ctx);
            }
            return;
        }
        if (key == keyboard::ControlKey::Enter) {
            announce_hourly(ctx);
        }
    }

    void handle_daily(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            phase_ = Phase::Menu;
            announce(ctx, "Weather menu. Daily selected");
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            if (list_index_ > 0) {
                --list_index_;
                announce_daily(ctx);
            }
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            if (list_index_ + 1 < daily_.size()) {
                ++list_index_;
                announce_daily(ctx);
            }
            return;
        }
        if (key == keyboard::ControlKey::Enter) {
            announce_daily(ctx);
        }
    }

    void announce_hourly(UiContext &ctx)
    {
        if (hourly_.empty()) {
            announce(ctx, "No hourly forecast");
            return;
        }
        const HourlyItem &item = hourly_[list_index_];
        std::string message =
            item.label + ". " + format_temperature(item.temperature) + ". " + item.description;
        if (!item.precip_probability.empty() && item.precip_probability != "0") {
            message += ". Rain chance " + item.precip_probability + " percent";
        }
        if (!item.humidity.empty() && item.humidity != "0") {
            message += ". Humidity " + item.humidity + " percent";
        }
        if (!item.uv_index.empty() && item.uv_index != "0") {
            message += ". UV " + item.uv_index;
        }
        announce(ctx, message);
    }

    void announce_daily(UiContext &ctx)
    {
        if (daily_.empty()) {
            announce(ctx, "No daily forecast");
            return;
        }
        const DailyItem &item = daily_[list_index_];
        std::string message = item.label + ". High " + format_temperature(item.temp_max) + ". Low " +
                              format_temperature(item.temp_min) + ". " + item.description;
        if (!item.precip_probability.empty() && item.precip_probability != "0") {
            message += ". Rain chance " + item.precip_probability + " percent";
        }
        if (!item.uv_index.empty() && item.uv_index != "0") {
            message += ". UV " + item.uv_index;
        }
        announce(ctx, message);
    }

    Phase phase_ = Phase::Loading;
    std::string location_;
    std::string current_summary_;
    std::string temperature_unit_;
    std::string city_buffer_;
    std::vector<HourlyItem> hourly_;
    std::vector<DailyItem> daily_;
    size_t menu_index_ = 0;
    size_t list_index_ = 0;
    std::string pending_announce_;
    bool background_fetch_pending_ = false;
};

} // namespace

std::unique_ptr<AppSession> make_weather_app()
{
    return std::make_unique<WeatherApp>();
}

} // namespace braillatron::ui
