#include "weather_backend.h"

#include "connect_config.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

int failures = 0;

void expect_true(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expect_eq(const std::string &actual, const std::string &expected, const char *message)
{
    if (actual != expected) {
        std::cerr << "FAIL: " << message << " (got '" << actual << "', expected '" << expected
                  << "')\n";
        ++failures;
    }
}

std::string temp_dir()
{
    const char *env = std::getenv("TMPDIR");
    const std::string base = (env != nullptr && env[0] != '\0') ? env : "/tmp";
    return base + "/braillatron-weather-self-test-" + std::to_string(::getpid());
}

const char *kFixture = R"({
  "latitude": 51.5,
  "longitude": -0.12,
  "current": {
    "time": "2026-06-13T12:00",
    "temperature_2m": 18.4,
    "relative_humidity_2m": 62,
    "weather_code": 3,
    "wind_speed_10m": 12.5,
    "uv_index": 3.2
  },
  "hourly": {
    "time": ["2026-06-13T12:00", "2026-06-13T13:00"],
    "temperature_2m": [18.4, 19.1],
    "weather_code": [3, 2],
    "precipitation_probability": [15, 20],
    "relative_humidity_2m": [62, 60],
    "uv_index": [3.2, 3.5]
  },
  "daily": {
    "time": ["2026-06-13", "2026-06-14"],
    "temperature_2m_max": [21.0, 22.5],
    "temperature_2m_min": [12.0, 13.5],
    "weather_code": [3, 2],
    "precipitation_probability_max": [40, 55],
    "uv_index_max": [4.0, 5.0]
  }
})";

bool test_weather_code_descriptions()
{
    expect_eq(braillatron::connect::WeatherBackend::describe_weather_code(0), "Clear sky",
              "code 0");
    expect_eq(braillatron::connect::WeatherBackend::describe_weather_code(3), "Overcast",
              "code 3");
    expect_eq(braillatron::connect::WeatherBackend::describe_weather_code(95), "Thunderstorm",
              "code 95");
    return true;
}

bool test_build_cache_from_fixture()
{
    const std::string dir = temp_dir();
    std::error_code ec;

    braillatron::connect::WeatherConfig config;
    config.cache_path = dir + "/cache.json";
    config.hourly_limit = 24;
    config.daily_limit = 7;
    config.temperature_unit = "celsius";

    braillatron::connect::WeatherBackend backend(config);
    const std::string cache_json =
        backend.build_cache_from_api(kFixture, 51.5, -0.12, "London");

    expect_true(cache_json.find("London") != std::string::npos, "location cached");
    expect_true(cache_json.find("Overcast") != std::string::npos, "current description");
    expect_true(cache_json.find("relative_humidity") != std::string::npos, "humidity cached");
    expect_true(cache_json.find("uv_index") != std::string::npos, "uv cached");
    expect_true(cache_json.find("precipitation_probability") != std::string::npos, "precip cached");
    expect_true(cache_json.find("Sat 2026-06-13") != std::string::npos ||
                    cache_json.find("Fri 2026-06-13") != std::string::npos,
                "friendly day label");
    expect_true(cache_json.find("\"hourly\"") != std::string::npos, "hourly section");
    expect_true(cache_json.find("\"daily\"") != std::string::npos, "daily section");
    expect_true(std::filesystem::exists(config.cache_path, ec), "cache file written");

    const std::string read_response = backend.read_cache();
    expect_true(read_response.find("\"ok\":true") != std::string::npos, "read cache ok");
    expect_true(read_response.find("London") != std::string::npos, "read cache location");

    const std::string status = backend.status();
    expect_true(status.find("\"cached\":true") != std::string::npos, "status cached");
    expect_true(status.find("18.4") != std::string::npos, "status temperature");

    const std::string alerts = backend.alerts();
    expect_true(alerts.find("\"ok\":true") != std::string::npos, "alerts ok");

    std::filesystem::remove_all(dir, ec);
    return true;
}

} // namespace

int main()
{
    if (!test_weather_code_descriptions()) {
        ++failures;
    }
    if (!test_build_cache_from_fixture()) {
        ++failures;
    }

    if (failures > 0) {
        std::cerr << failures << " weather self-test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "weather self-test passed\n";
    return EXIT_SUCCESS;
}
