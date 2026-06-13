#pragma once

#include "connect_config.h"

#include <cstdint>
#include <string>

namespace braillatron::connect {

class WeatherBackend {
public:
    explicit WeatherBackend(WeatherConfig config);

    std::string fetch();
    std::string read_cache() const;
    std::string status() const;

    static std::string describe_weather_code(int code);
    std::string build_cache_from_api(const std::string &api_json, double latitude, double longitude,
                                     const std::string &location_name);

private:
    bool resolve_coordinates(double &latitude, double &longitude, std::string &location_name);
    std::string build_forecast_url(double latitude, double longitude) const;
    std::string curl_fetch(const std::string &url) const;
    bool save_cache(const std::string &cache_json) const;
    std::string load_cache_file() const;
    bool cache_is_fresh(const std::string &cache_json) const;
    uint64_t cache_age_sec(const std::string &cache_json) const;

    WeatherConfig config_;
};

} // namespace braillatron::connect
