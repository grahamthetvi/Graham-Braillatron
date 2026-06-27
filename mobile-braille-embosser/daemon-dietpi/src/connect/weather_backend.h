#pragma once

#include "connect_config.h"

#include <cstdint>
#include <string>

namespace braillatron::connect {

class EventWriter;

class WeatherBackend {
public:
    WeatherBackend(WeatherConfig config, EventWriter *events = nullptr);

    std::string fetch();
    std::string read_cache() const;
    std::string status() const;
    std::string config_status() const;
    std::string set_location(const std::string &city_name);
    std::string set_temperature_unit(const std::string &unit);
    std::string alerts() const;
    void poll_refresh(uint64_t now_sec);

    static std::string describe_weather_code(int code);
    std::string build_cache_from_api(const std::string &api_json, double latitude, double longitude,
                                     const std::string &location_name);

private:
    bool resolve_coordinates(double &latitude, double &longitude, std::string &location_name);
    std::string effective_temperature_unit() const;
    std::string build_forecast_url(double latitude, double longitude) const;
    std::string curl_fetch(const std::string &url) const;
    bool save_cache(const std::string &cache_json) const;
    bool save_config() const;
    std::string load_cache_file() const;
    bool cache_is_fresh(const std::string &cache_json) const;
    uint64_t cache_age_sec(const std::string &cache_json) const;
    std::string build_alerts_json(const std::string &cache_json) const;
    void evaluate_and_emit_alerts(const std::string &cache_json);
    void emit_weather_updated(const std::string &cache_json);

    WeatherConfig config_;
    EventWriter *events_ = nullptr;
    std::string resolved_country_code_;
    uint64_t last_poll_refresh_sec_ = 0;
    std::string last_alert_signature_;
};

} // namespace braillatron::connect
