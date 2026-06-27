#include "../../connect/connect_client.h"
#include "../../connect/json_utils.h"
#include "app_registry.h"
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

enum class NavKind {
    Current,
    Hourly,
    Daily,
    Location,
    Refresh,
};

struct NavItem {
    NavKind kind = NavKind::Current;
    size_t index = 0;
};

enum class Phase {
    Loading,
    Browse,
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
            enter_browse(fresh ? "" : "Showing cached forecast");
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
            if (announce_nav_after_ready_) {
                announce_nav_after_ready_ = false;
                announce_nav_item(ctx);
            }
            return;
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
        case Phase::Browse:
            handle_browse(key, ctx);
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
        if (event.type != "weather.updated" || ctx.connect == nullptr) {
            return;
        }

        if (phase_ == Phase::Loading || background_fetch_pending_) {
            const std::string read_response = ctx.connect->request("weather.read");
            if (braillatron::connect::json_get_bool(read_response, "ok", false)) {
                const bool fresh = braillatron::connect::json_get_bool(read_response, "fresh", false);
                parse_cache_response(read_response, !fresh);
                enter_browse(background_fetch_pending_ ? "Forecast updated"
                                                       : build_ready_suffix(fresh ? "" : "Showing cached forecast"));
            }
            background_fetch_pending_ = false;
            return;
        }

        pending_announce_ = "Forecast updated";
    }

private:
    void reset_session()
    {
        phase_ = Phase::Loading;
        location_.clear();
        current_summary_.clear();
        hourly_.clear();
        daily_.clear();
        nav_items_.clear();
        nav_index_ = 0;
        pending_announce_.clear();
        temperature_unit_ = "celsius";
        city_buffer_.clear();
        background_fetch_pending_ = false;
        loading_nav_index_ = 0;
        announce_nav_after_ready_ = false;
    }

    void start_fetch(UiContext &ctx, bool background)
    {
        background_fetch_pending_ = background;
        if (!background) {
            loading_nav_index_ = nav_index_;
            phase_ = Phase::Loading;
        }
        ctx.connect->request_async("weather.fetch", "", [this, background](const std::string &response) {
            if (!braillatron::connect::json_get_bool(response, "ok", false)) {
                if (!background) {
                    pending_announce_ = "Weather fetch failed";
                    phase_ = Phase::Browse;
                    nav_index_ = loading_nav_index_;
                }
                background_fetch_pending_ = false;
                return;
            }
            const bool stale = braillatron::connect::json_get_bool(response, "stale", false);
            parse_cache_response(response, stale);
            if (background) {
                pending_announce_ = "Forecast updated";
            } else {
                enter_browse(stale ? "Showing cached forecast" : "");
            }
            background_fetch_pending_ = false;
        });
    }

    void start_background_fetch(UiContext &ctx) { start_fetch(ctx, true); }

    void enter_browse(const std::string &suffix)
    {
        phase_ = Phase::Browse;
        rebuild_nav_items();
        if (nav_index_ >= nav_items_.size()) {
            nav_index_ = 0;
        }
        pending_announce_ = build_ready_announcement(suffix);
        announce_nav_after_ready_ = !nav_items_.empty();
    }

    std::string build_ready_suffix(const std::string &suffix) const { return suffix; }

    std::string build_ready_announcement(const std::string &suffix) const
    {
        std::string message =
            location_.empty() ? "Weather ready" : "Weather for " + location_;
        if (!suffix.empty()) {
            message += ". " + suffix;
        }
        message += ". Use up and down to browse. Press Enter on Location or Refresh.";
        if (!nav_items_.empty()) {
            message += " " + nav_position_label();
        }
        return message;
    }

    std::string nav_position_label() const
    {
        if (nav_items_.empty()) {
            return {};
        }
        return std::to_string(nav_index_ + 1) + " of " + std::to_string(nav_items_.size());
    }

    void rebuild_nav_items()
    {
        nav_items_.clear();
        nav_items_.push_back({NavKind::Current, 0});
        for (size_t i = 0; i < hourly_.size(); ++i) {
            nav_items_.push_back({NavKind::Hourly, i});
        }
        for (size_t i = 0; i < daily_.size(); ++i) {
            nav_items_.push_back({NavKind::Daily, i});
        }
        nav_items_.push_back({NavKind::Location, 0});
        nav_items_.push_back({NavKind::Refresh, 0});
    }

    void parse_cache_response(const std::string &response, bool stale)
    {
        (void)stale;
        const size_t cache_pos = response.find("\"cache\":{");
        if (cache_pos == std::string::npos) {
            pending_announce_ = "Weather data unavailable";
            phase_ = Phase::Browse;
            rebuild_nav_items();
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

                current_summary_ = format_temperature(temp);
                if (!desc.empty()) {
                    current_summary_ += ". " + desc;
                }
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

        rebuild_nav_items();
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

    void handle_browse(keyboard::ControlKey key, UiContext &ctx)
    {
        if (nav_items_.empty()) {
            if (key == keyboard::ControlKey::Backspace && ctx.registry != nullptr) {
                ctx.registry->exit();
            }
            return;
        }

        if (key == keyboard::ControlKey::DpadUp) {
            if (nav_index_ > 0) {
                --nav_index_;
                announce_nav_item(ctx);
            }
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            if (nav_index_ + 1 < nav_items_.size()) {
                ++nav_index_;
                announce_nav_item(ctx);
            }
            return;
        }
        if (key == keyboard::ControlKey::Backspace) {
            if (ctx.registry != nullptr) {
                ctx.registry->exit();
            }
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        const NavItem &item = nav_items_[nav_index_];
        if (item.kind == NavKind::Location) {
            phase_ = Phase::Location;
            city_buffer_.clear();
            announce(ctx, "Enter city name and press Enter. Current location " +
                               (location_.empty() ? "not set" : location_));
            return;
        }
        if (item.kind == NavKind::Refresh) {
            announce(ctx, "Refreshing forecast");
            start_fetch(ctx, false);
            return;
        }
        announce_nav_item(ctx);
    }

    void handle_location(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!city_buffer_.empty()) {
                city_buffer_.pop_back();
            } else {
                phase_ = Phase::Browse;
                announce(ctx, "Location. " + nav_position_label());
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

        loading_nav_index_ = nav_index_;
        phase_ = Phase::Loading;
        announce(ctx, "Updating location to " + city_buffer_);
        const std::string fields =
            "\"city_name\":\"" + braillatron::connect::json_escape(city_buffer_) + "\"";
        ctx.connect->request_async("weather.set_location", fields, [this](const std::string &response) {
            if (!braillatron::connect::json_get_bool(response, "ok", false)) {
                pending_announce_ = "Location update failed";
                phase_ = Phase::Browse;
                nav_index_ = loading_nav_index_;
                return;
            }
            parse_cache_response(response, false);
            enter_browse("");
        });
    }

    void announce_nav_item(UiContext &ctx)
    {
        if (nav_items_.empty()) {
            announce(ctx, "No forecast data");
            return;
        }

        const NavItem &item = nav_items_[nav_index_];
        std::string message;
        switch (item.kind) {
        case NavKind::Current:
            message = "Current. ";
            message += current_summary_.empty() ? "No current conditions" : current_summary_;
            break;
        case NavKind::Hourly:
            if (item.index >= hourly_.size()) {
                message = "No hourly forecast";
                break;
            }
            message = hourly_message(hourly_[item.index]);
            break;
        case NavKind::Daily:
            if (item.index >= daily_.size()) {
                message = "No daily forecast";
                break;
            }
            message = daily_message(daily_[item.index]);
            break;
        case NavKind::Location:
            message = "Location. ";
            message += location_.empty() ? "Not set. Press Enter to change." : location_ + ". Press Enter to change.";
            break;
        case NavKind::Refresh:
            message = "Refresh forecast. Press Enter to refresh.";
            break;
        }
        message += ". " + nav_position_label();
        announce(ctx, message);
    }

    std::string hourly_message(const HourlyItem &item) const
    {
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
        return message;
    }

    std::string daily_message(const DailyItem &item) const
    {
        std::string message = item.label + ". High " + format_temperature(item.temp_max) + ". Low " +
                              format_temperature(item.temp_min) + ". " + item.description;
        if (!item.precip_probability.empty() && item.precip_probability != "0") {
            message += ". Rain chance " + item.precip_probability + " percent";
        }
        if (!item.uv_index.empty() && item.uv_index != "0") {
            message += ". UV " + item.uv_index;
        }
        return message;
    }

    Phase phase_ = Phase::Loading;
    std::string location_;
    std::string current_summary_;
    std::string temperature_unit_;
    std::string city_buffer_;
    std::vector<HourlyItem> hourly_;
    std::vector<DailyItem> daily_;
    std::vector<NavItem> nav_items_;
    size_t nav_index_ = 0;
    size_t loading_nav_index_ = 0;
    std::string pending_announce_;
    bool background_fetch_pending_ = false;
    bool announce_nav_after_ready_ = false;
};

} // namespace

std::unique_ptr<AppSession> make_weather_app()
{
    return std::make_unique<WeatherApp>();
}

} // namespace braillatron::ui
