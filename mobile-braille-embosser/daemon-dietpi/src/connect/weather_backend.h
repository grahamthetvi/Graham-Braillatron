#pragma once

#include "connect_config.h"

#include <array>
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
    std::string list_cities() const;
    std::string select_city(size_t slot);
    std::string set_location(const std::string &city_name);
    std::string set_city(size_t slot, const std::string &city_name,
                         const std::string &region = {}, const std::string &country = {});
    std::string detect_ip_location() const;
    std::string set_city_from_ip(size_t slot);
    std::string set_temperature_unit(const std::string &unit);
    std::string alerts() const;
    void poll_refresh(uint64_t now_sec);

    static std::string describe_weather_code(int code);
    std::string build_cache_from_api(const std::string &api_json, double latitude, double longitude,
                                     const std::string &location_name, size_t slot);

private:
    bool resolve_coordinates(WeatherCitySlot &city, std::string &location_name,
                             std::string &country_code, const std::string &region_hint = {},
                             const std::string &country_hint = {});
    std::string effective_temperature_unit_for(const std::string &country_code, double latitude,
                                               double longitude) const;
    std::string build_forecast_url(double latitude, double longitude,
                                   const std::string &temperature_unit) const;
    std::string curl_fetch(const std::string &url) const;
    bool save_cache(const std::string &cache_json, size_t slot) const;
    bool save_config() const;
    std::string load_cache_file(size_t slot) const;
    std::string slot_cache_path(size_t slot) const;
    bool cache_is_fresh(const std::string &cache_json) const;
    uint64_t cache_age_sec(const std::string &cache_json) const;
    std::string build_alerts_json(const std::string &cache_json) const;
    void evaluate_and_emit_alerts(const std::string &cache_json);
    void emit_weather_updated(const std::string &cache_json);
    std::string fetch_slot(size_t slot);
    void sync_legacy_fields_from_active();

    WeatherConfig config_;
    EventWriter *events_ = nullptr;
    std::array<std::string, WeatherConfig::kMaxCities> resolved_country_codes_ {};
    uint64_t last_poll_refresh_sec_ = 0;
    std::string last_alert_signature_;
};

} // namespace braillatron::connect
