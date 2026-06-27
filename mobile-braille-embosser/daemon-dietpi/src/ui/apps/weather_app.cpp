#include "../../connect/connect_client.h"
#include "../../connect/json_utils.h"
#include "../layered_browse_list.h"
#include "../output_hub.h"
#include "app_registry.h"
#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include <functional>
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

struct CitySlotInfo {
    size_t slot = 0;
    std::string label;
    std::string name;
    bool configured = false;
    std::string summary;
};

enum class Layer {
    Cities,
    Forecast,
    ReplaceSlot,
    EnterCity,
    Loading,
};

enum class ForecastKind {
    Current,
    Hourly,
    Daily,
    Refresh,
};

struct ForecastNavItem {
    ForecastKind kind = ForecastKind::Current;
    size_t index = 0;
};

constexpr size_t kMaxCities = 3;
constexpr size_t kAddCityIndex = kMaxCities;

class WeatherApp final : public AppSession {
public:
    std::string id() const override { return "weather"; }
    std::string label() const override { return "Weather"; }
    AppKind kind() const override { return AppKind::Standalone; }

    bool browse_list_active() const override
    {
        return layer_ == Layer::Cities || layer_ == Layer::Forecast || layer_ == Layer::ReplaceSlot;
    }

    const LayeredBrowseList *browse_list() const override { return &browse_; }

    std::string browse_breadcrumb() const override
    {
        switch (layer_) {
        case Layer::Forecast:
            return location_.empty() ? "Weather" : "Weather > " + location_;
        case Layer::ReplaceSlot:
            return "Weather > Add city";
        case Layer::EnterCity:
            return "Weather > Add city > " + slot_label(pending_slot_);
        default:
            return {};
        }
    }

    std::string composer_line() const override
    {
        return layer_ == Layer::EnterCity ? city_buffer_ : std::string {};
    }

    bool buffers_braille_words() const override { return layer_ == Layer::EnterCity; }

    void on_enter(UiContext &ctx) override
    {
        reset_session();
        ctx_ = &ctx;
        if (ctx.connect == nullptr) {
            announce(ctx, "Connectivity unavailable.");
            return;
        }

        announce(ctx, "Weather loading.");
        load_city_list(ctx);
    }

    void on_exit(UiContext &ctx) override
    {
        reset_session();
        ctx_ = nullptr;
        announce(ctx, "Weather closed");
    }

    void on_poll(UiContext &ctx) override
    {
        if (!pending_announce_.empty()) {
            announce(ctx, pending_announce_);
            pending_announce_.clear();
            if (announce_forecast_after_ready_) {
                announce_forecast_after_ready_ = false;
                announce_forecast_item(ctx);
            }
            return;
        }
    }

    void on_chord(uint8_t, UiContext &) override {}

    void on_text(const std::string &text, UiContext &) override
    {
        if (layer_ != Layer::EnterCity || text.empty()) {
            return;
        }
        city_buffer_ += text;
        sync_chrome();
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed || layer_ == Layer::Loading) {
            return;
        }

        switch (layer_) {
        case Layer::Cities:
            handle_cities(key, ctx);
            break;
        case Layer::Forecast:
            handle_forecast(key, ctx);
            break;
        case Layer::ReplaceSlot:
            handle_replace_slot(key, ctx);
            break;
        case Layer::EnterCity:
            handle_enter_city(key, ctx);
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

        if (layer_ == Layer::Loading || background_fetch_pending_) {
            if (selected_slot_ == active_slot_) {
                load_forecast_for_slot(ctx, selected_slot_, background_fetch_pending_);
            } else {
                layer_ = Layer::Cities;
                load_city_list(ctx);
            }
            background_fetch_pending_ = false;
            return;
        }

        pending_announce_ = "Forecast updated";
    }

private:
    static std::string slot_label(size_t slot)
    {
        static const char *kLabels[] = {"City A", "City B", "City C"};
        return slot < kMaxCities ? kLabels[slot] : "City";
    }

    void reset_session()
    {
        layer_ = Layer::Loading;
        cities_.clear();
        active_slot_ = 0;
        selected_slot_ = 0;
        pending_slot_ = 0;
        location_.clear();
        current_summary_.clear();
        hourly_.clear();
        daily_.clear();
        forecast_nav_.clear();
        browse_.clear();
        temperature_unit_ = "celsius";
        city_buffer_.clear();
        pending_announce_.clear();
        background_fetch_pending_ = false;
        announce_forecast_after_ready_ = false;
        saved_city_focus_ = 0;
        saved_forecast_focus_ = 0;
    }

    void sync_chrome(bool at_boundary = false)
    {
        if (ctx_ != nullptr && ctx_->output != nullptr) {
            ctx_->output->sync_chrome(at_boundary);
        }
    }

    void load_city_list(UiContext &ctx)
    {
        const std::string response = ctx.connect->request("weather.list");
        cities_.clear();
        active_slot_ = 0;
        if (!braillatron::connect::json_get_bool(response, "ok", false)) {
            pending_announce_ = "Weather unavailable";
            layer_ = Layer::Cities;
            rebuild_city_list();
            sync_chrome();
            return;
        }

        active_slot_ = static_cast<size_t>(std::stoul(
            braillatron::connect::json_get_string(response, "active_slot").empty()
                ? "0"
                : braillatron::connect::json_get_string(response, "active_slot")));

        const size_t cities_pos = response.find("\"cities\":[");
        if (cities_pos != std::string::npos) {
            const size_t cities_end = response.find(']', cities_pos);
            const std::string cities_array =
                response.substr(cities_pos + 9, cities_end - cities_pos - 9);
            for (const auto &item :
                 braillatron::connect::json_split_objects("[" + cities_array + "]")) {
                CitySlotInfo city;
                const std::string slot_str = braillatron::connect::json_get_string(item, "slot");
                city.slot = slot_str.empty() ? 0 : static_cast<size_t>(std::stoul(slot_str));
                city.label = braillatron::connect::json_get_string(item, "label");
                city.name = braillatron::connect::json_get_string(item, "name");
                city.configured =
                    braillatron::connect::json_get_bool(item, "configured", !city.name.empty());
                city.summary = braillatron::connect::json_get_string(item, "summary");
                if (city.slot < kMaxCities) {
                    cities_.push_back(std::move(city));
                }
            }
        }

        layer_ = Layer::Cities;
        rebuild_city_list();
        pending_announce_ = "Weather. Use up and down to browse cities. Press Enter to open.";
        sync_chrome();
        if (!browse_.empty()) {
            browse_.announce_focus(ctx.output, false);
        }
    }

    std::string city_row_label(const CitySlotInfo &city) const
    {
        std::string row = city.label + ": ";
        row += city.name.empty() ? "(empty)" : city.name;
        if (!city.summary.empty() && city.configured) {
            row += " — " + city.summary;
        }
        return row;
    }

    void rebuild_city_list()
    {
        std::vector<BrowseListItem> items;
        for (const CitySlotInfo &city : cities_) {
            BrowseListItem item;
            item.label = city_row_label(city);
            items.push_back(std::move(item));
        }
        BrowseListItem add_item;
        add_item.label = "Add New City";
        items.push_back(std::move(add_item));

        browse_.set_container_name("Weather cities");
        browse_.set_items(std::move(items));
        if (saved_city_focus_ < browse_.size()) {
            browse_.set_focus(saved_city_focus_);
        }
    }

    void rebuild_replace_slot_list()
    {
        std::vector<BrowseListItem> items;
        for (size_t i = 0; i < kMaxCities; ++i) {
            BrowseListItem item;
            item.label = "Replace " + slot_label(i);
            items.push_back(std::move(item));
        }
        browse_.set_container_name("Replace city slot");
        browse_.set_items(std::move(items));
        browse_.reset_focus();
    }

    void handle_cities(keyboard::ControlKey key, UiContext &ctx)
    {
        if (browse_.empty()) {
            if (key == keyboard::ControlKey::Backspace && ctx.registry != nullptr) {
                ctx.registry->exit();
            }
            return;
        }

        if (key == keyboard::ControlKey::DpadUp || key == keyboard::ControlKey::DpadDown) {
            browse_.handle_control(key, ctx.output);
            saved_city_focus_ = browse_.focus_index();
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

        const size_t index = browse_.focus_index();
        saved_city_focus_ = index;
        if (index == kAddCityIndex) {
            begin_add_city(ctx);
            return;
        }

        if (index >= cities_.size()) {
            return;
        }

        const CitySlotInfo &city = cities_[index];
        if (!city.configured) {
            pending_slot_ = city.slot;
            begin_enter_city(ctx, "Enter city for " + city.label + ".");
            return;
        }

        open_city_forecast(ctx, city.slot);
    }

    void begin_add_city(UiContext &ctx)
    {
        for (const CitySlotInfo &city : cities_) {
            if (!city.configured) {
                pending_slot_ = city.slot;
                begin_enter_city(ctx, "Enter new city for " + city.label + ".");
                return;
            }
        }

        layer_ = Layer::ReplaceSlot;
        rebuild_replace_slot_list();
        sync_chrome();
        announce(ctx, "All city slots full. Choose a slot to replace.");
        browse_.announce_focus(ctx.output, false);
    }

    void handle_replace_slot(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::DpadUp || key == keyboard::ControlKey::DpadDown) {
            browse_.handle_control(key, ctx.output);
            return;
        }

        if (key == keyboard::ControlKey::Backspace) {
            layer_ = Layer::Cities;
            rebuild_city_list();
            sync_chrome();
            browse_.announce_focus(ctx.output, false);
            return;
        }

        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        pending_slot_ = browse_.focus_index();
        begin_enter_city(ctx, "Replace " + slot_label(pending_slot_) + ". Enter city name.");
    }

    void begin_enter_city(UiContext &ctx, const std::string &message)
    {
        layer_ = Layer::EnterCity;
        city_buffer_.clear();
        sync_chrome();
        announce(ctx, message);
    }

    void handle_enter_city(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!city_buffer_.empty()) {
                city_buffer_.pop_back();
                sync_chrome();
                return;
            }
            layer_ = Layer::Cities;
            rebuild_city_list();
            sync_chrome();
            browse_.announce_focus(ctx.output, false);
            return;
        }

        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        if (city_buffer_.empty()) {
            announce(ctx, "Type a city name first");
            return;
        }

        layer_ = Layer::Loading;
        selected_slot_ = pending_slot_;
        announce(ctx, "Updating " + slot_label(pending_slot_) + " to " + city_buffer_);
        const std::string fields = "\"slot\":\"" + std::to_string(pending_slot_) +
                                   "\",\"city_name\":\"" +
                                   braillatron::connect::json_escape(city_buffer_) + "\"";
        ctx.connect->request_async("weather.set_city", fields, [this](const std::string &response) {
            if (!braillatron::connect::json_get_bool(response, "ok", false)) {
                pending_announce_ = "City update failed";
                layer_ = Layer::Cities;
                return;
            }
            parse_cache_response(response, false);
            layer_ = Layer::Forecast;
            rebuild_forecast_list();
            pending_announce_ = "Weather for " + location_ + ". Use up and down to browse.";
            announce_forecast_after_ready_ = true;
        });
    }

    void open_city_forecast(UiContext &ctx, size_t slot)
    {
        selected_slot_ = slot;
        const std::string fields = "\"slot\":\"" + std::to_string(slot) + "\"";
        const std::string response = ctx.connect->request("weather.select", fields);
        if (!braillatron::connect::json_get_bool(response, "ok", false)) {
            announce(ctx, "Could not open city forecast");
            return;
        }

        if (response.find("\"cache\":") != std::string::npos) {
            const bool fresh = braillatron::connect::json_get_bool(response, "fresh", false);
            parse_cache_response(response, !fresh);
            enter_forecast(fresh ? "" : "Showing cached forecast");
            start_background_fetch(ctx);
            return;
        }

        layer_ = Layer::Loading;
        announce(ctx, "Fetching forecast.");
        ctx.connect->request_async("weather.fetch", "", [this](const std::string &fetch_response) {
            if (!braillatron::connect::json_get_bool(fetch_response, "ok", false)) {
                pending_announce_ = "Weather fetch failed";
                layer_ = Layer::Cities;
                return;
            }
            const bool stale = braillatron::connect::json_get_bool(fetch_response, "stale", false);
            parse_cache_response(fetch_response, stale);
            enter_forecast(stale ? "Showing cached forecast" : "");
        });
    }

    void load_forecast_for_slot(UiContext &ctx, size_t slot, bool background)
    {
        const std::string fields = "\"slot\":\"" + std::to_string(slot) + "\"";
        const std::string response = ctx.connect->request("weather.select", fields);
        if (response.find("\"cache\":") != std::string::npos) {
            const bool fresh = braillatron::connect::json_get_bool(response, "fresh", false);
            parse_cache_response(response, !fresh);
            enter_forecast(background ? "Forecast updated"
                                      : (fresh ? "" : "Showing cached forecast"));
            return;
        }

        if (!background) {
            layer_ = Layer::Loading;
        }
        start_fetch(ctx, background);
    }

    void start_background_fetch(UiContext &ctx) { start_fetch(ctx, true); }

    void start_fetch(UiContext &ctx, bool background)
    {
        background_fetch_pending_ = background;
        if (!background) {
            layer_ = Layer::Loading;
        }
        ctx.connect->request_async("weather.fetch", "", [this, background](const std::string &response) {
            if (!braillatron::connect::json_get_bool(response, "ok", false)) {
                if (!background) {
                    pending_announce_ = "Weather fetch failed";
                    layer_ = Layer::Forecast;
                }
                background_fetch_pending_ = false;
                return;
            }
            const bool stale = braillatron::connect::json_get_bool(response, "stale", false);
            parse_cache_response(response, stale);
            if (background) {
                pending_announce_ = "Forecast updated";
            } else {
                enter_forecast(stale ? "Showing cached forecast" : "");
            }
            background_fetch_pending_ = false;
        });
    }

    void enter_forecast(const std::string &suffix)
    {
        layer_ = Layer::Forecast;
        rebuild_forecast_list();
        std::string message =
            location_.empty() ? "Weather ready" : "Weather for " + location_;
        if (!suffix.empty()) {
            message += ". " + suffix;
        }
        message += ". Use up and down to browse.";
        pending_announce_ = message;
        announce_forecast_after_ready_ = !forecast_nav_.empty();
        sync_chrome();
    }

    void rebuild_forecast_list()
    {
        forecast_nav_.clear();
        forecast_nav_.push_back({ForecastKind::Current, 0});
        for (size_t i = 0; i < hourly_.size(); ++i) {
            forecast_nav_.push_back({ForecastKind::Hourly, i});
        }
        for (size_t i = 0; i < daily_.size(); ++i) {
            forecast_nav_.push_back({ForecastKind::Daily, i});
        }
        forecast_nav_.push_back({ForecastKind::Refresh, 0});

        std::vector<BrowseListItem> items;
        for (const ForecastNavItem &nav : forecast_nav_) {
            BrowseListItem item;
            switch (nav.kind) {
            case ForecastKind::Current:
                item.label = "Current: " +
                             (current_summary_.empty() ? "No current conditions" : current_summary_);
                break;
            case ForecastKind::Hourly:
                if (nav.index < hourly_.size()) {
                    item.label = hourly_.at(nav.index).label + ": " +
                                 format_temperature(hourly_.at(nav.index).temperature) + " " +
                                 hourly_.at(nav.index).description;
                } else {
                    item.label = "Hourly forecast";
                }
                break;
            case ForecastKind::Daily:
                if (nav.index < daily_.size()) {
                    const DailyItem &day = daily_.at(nav.index);
                    item.label = day.label + ": High " + format_temperature(day.temp_max) +
                                 " Low " + format_temperature(day.temp_min);
                } else {
                    item.label = "Daily forecast";
                }
                break;
            case ForecastKind::Refresh:
                item.label = "Refresh forecast";
                break;
            }
            items.push_back(std::move(item));
        }

        browse_.set_container_name(location_.empty() ? "Weather forecast" : location_);
        browse_.set_items(std::move(items));
        if (saved_forecast_focus_ < browse_.size()) {
            browse_.set_focus(saved_forecast_focus_);
        }
    }

    void handle_forecast(keyboard::ControlKey key, UiContext &ctx)
    {
        if (browse_.empty()) {
            if (key == keyboard::ControlKey::Backspace) {
                return_to_cities(ctx);
            }
            return;
        }

        if (key == keyboard::ControlKey::DpadUp || key == keyboard::ControlKey::DpadDown) {
            browse_.handle_control(key, ctx.output);
            saved_forecast_focus_ = browse_.focus_index();
            return;
        }

        if (key == keyboard::ControlKey::Backspace) {
            return_to_cities(ctx);
            return;
        }

        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        saved_forecast_focus_ = browse_.focus_index();
        if (browse_.focus_index() >= forecast_nav_.size()) {
            return;
        }

        const ForecastNavItem &nav = forecast_nav_[browse_.focus_index()];
        if (nav.kind == ForecastKind::Refresh) {
            announce(ctx, "Refreshing forecast");
            start_fetch(ctx, false);
            return;
        }

        announce_forecast_item(ctx);
    }

    void return_to_cities(UiContext &ctx)
    {
        layer_ = Layer::Cities;
        load_city_list(ctx);
    }

    void announce_forecast_item(UiContext &ctx)
    {
        if (browse_.focus_index() >= forecast_nav_.size()) {
            announce(ctx, "No forecast data");
            return;
        }

        const ForecastNavItem &nav = forecast_nav_[browse_.focus_index()];
        std::string message;
        switch (nav.kind) {
        case ForecastKind::Current:
            message = "Current. ";
            message += current_summary_.empty() ? "No current conditions" : current_summary_;
            break;
        case ForecastKind::Hourly:
            message = nav.index < hourly_.size() ? hourly_message(hourly_[nav.index])
                                                 : "No hourly forecast";
            break;
        case ForecastKind::Daily:
            message =
                nav.index < daily_.size() ? daily_message(daily_[nav.index]) : "No daily forecast";
            break;
        case ForecastKind::Refresh:
            message = "Refresh forecast. Press Enter to refresh.";
            break;
        }
        message += ". " + browse_.position_label();
        announce(ctx, message);
    }

    void parse_cache_response(const std::string &response, bool stale)
    {
        (void)stale;
        const size_t cache_pos = response.find("\"cache\":{");
        if (cache_pos == std::string::npos) {
            pending_announce_ = "Weather data unavailable";
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

    Layer layer_ = Layer::Loading;
    UiContext *ctx_ = nullptr;
    std::vector<CitySlotInfo> cities_;
    size_t active_slot_ = 0;
    size_t selected_slot_ = 0;
    size_t pending_slot_ = 0;
    size_t saved_city_focus_ = 0;
    size_t saved_forecast_focus_ = 0;
    std::string location_;
    std::string current_summary_;
    std::string temperature_unit_;
    std::string city_buffer_;
    std::vector<HourlyItem> hourly_;
    std::vector<DailyItem> daily_;
    std::vector<ForecastNavItem> forecast_nav_;
    LayeredBrowseList browse_;
    std::string pending_announce_;
    bool background_fetch_pending_ = false;
    bool announce_forecast_after_ready_ = false;
};

} // namespace

std::unique_ptr<AppSession> make_weather_app()
{
    return std::make_unique<WeatherApp>();
}

} // namespace braillatron::ui
