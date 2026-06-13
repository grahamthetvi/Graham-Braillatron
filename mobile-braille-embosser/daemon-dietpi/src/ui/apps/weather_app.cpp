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
};

struct DailyItem {
    std::string label;
    std::string description;
    std::string temp_max;
    std::string temp_min;
};

enum class Phase {
    Loading,
    Menu,
    Current,
    Hourly,
    Daily,
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
        phase_ = Phase::Loading;
        announce(ctx, "Weather. Fetching forecast.");
        ctx.connect->request_async("weather.fetch", "", [this](const std::string &response) {
            parse_fetch_response(response);
        });
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
    void on_text(const std::string &, UiContext &) override {}

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
        default:
            break;
        }
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
    }

    void parse_fetch_response(const std::string &response)
    {
        if (!braillatron::connect::json_get_bool(response, "ok", false)) {
            pending_announce_ = "Weather fetch failed";
            phase_ = Phase::Menu;
            return;
        }

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
                current_summary_ = format_temperature(temp) + ". " + desc;
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
                if (!entry.label.empty()) {
                    daily_.push_back(entry);
                }
            }
        }

        phase_ = Phase::Menu;
        menu_index_ = 0;
        const bool stale = braillatron::connect::json_get_bool(response, "stale", false);
        pending_announce_ = (location_.empty() ? "Weather ready" : "Weather for " + location_) +
                            (stale ? ". Showing cached forecast" : "") +
                            ". Current, Hourly, or Daily. Press Enter.";
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
        static const std::vector<std::string> kMenuItems = {"Current", "Hourly", "Daily", "Refresh"};

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
            phase_ = Phase::Loading;
            announce(ctx, "Refreshing forecast");
            ctx.connect->request_async("weather.fetch", "", [this](const std::string &response) {
                parse_fetch_response(response);
            });
        }
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
        announce(ctx, item.label + ". " + format_temperature(item.temperature) + ". " +
                           item.description);
    }

    void announce_daily(UiContext &ctx)
    {
        if (daily_.empty()) {
            announce(ctx, "No daily forecast");
            return;
        }
        const DailyItem &item = daily_[list_index_];
        announce(ctx, item.label + ". High " + format_temperature(item.temp_max) + ". Low " +
                           format_temperature(item.temp_min) + ". " + item.description);
    }

    Phase phase_ = Phase::Loading;
    std::string location_;
    std::string current_summary_;
    std::string temperature_unit_;
    std::vector<HourlyItem> hourly_;
    std::vector<DailyItem> daily_;
    size_t menu_index_ = 0;
    size_t list_index_ = 0;
    std::string pending_announce_;
};

} // namespace

std::unique_ptr<AppSession> make_weather_app()
{
    return std::make_unique<WeatherApp>();
}

} // namespace braillatron::ui
