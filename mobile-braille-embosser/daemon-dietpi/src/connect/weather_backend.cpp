#include "weather_backend.h"

#include "event_writer.h"
#include "json_utils.h"
#include "subprocess.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace braillatron::connect {

namespace {

constexpr const char *kUserAgent = "Braillatron/1.0 (accessibility device)";

uint64_t unix_now_sec()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

std::string url_encode(const std::string &value)
{
    std::ostringstream out;
    out << std::hex << std::uppercase;
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out << static_cast<char>(ch);
        } else if (ch == ' ') {
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        } else {
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
        }
    }
    return out.str();
}

std::string find_json_object(const std::string &json, const std::string &key)
{
    const std::string needle = "\"" + key + "\":";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    size_t start = pos + needle.size();
    while (start < json.size() && std::isspace(static_cast<unsigned char>(json[start]))) {
        ++start;
    }
    if (start >= json.size() || json[start] != '{') {
        return {};
    }
    int depth = 0;
    for (size_t i = start; i < json.size(); ++i) {
        if (json[i] == '{') {
            ++depth;
        } else if (json[i] == '}') {
            --depth;
            if (depth == 0) {
                return json.substr(start, i - start + 1);
            }
        }
    }
    return {};
}

std::string extract_array_body(const std::string &json, const std::string &section,
                               const std::string &key)
{
    const std::string section_obj = find_json_object(json, section);
    if (section_obj.empty()) {
        return {};
    }
    const std::string needle = "\"" + key + "\":";
    const size_t pos = section_obj.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    size_t start = pos + needle.size();
    while (start < section_obj.size() && std::isspace(static_cast<unsigned char>(section_obj[start]))) {
        ++start;
    }
    if (start >= section_obj.size() || section_obj[start] != '[') {
        return {};
    }
    ++start;
    const size_t end = section_obj.find(']', start);
    if (end == std::string::npos) {
        return {};
    }
    return section_obj.substr(start, end - start);
}

std::vector<std::string> split_json_string_array(const std::string &array_body)
{
    std::vector<std::string> values;
    for (size_t i = 0; i < array_body.size();) {
        const size_t quote = array_body.find('"', i);
        if (quote == std::string::npos) {
            break;
        }
        std::string value;
        for (size_t j = quote + 1; j < array_body.size(); ++j) {
            if (array_body[j] == '\\' && j + 1 < array_body.size()) {
                value += array_body[j + 1];
                ++j;
                continue;
            }
            if (array_body[j] == '"') {
                values.push_back(value);
                i = j + 1;
                break;
            }
            value += array_body[j];
        }
        if (i <= quote) {
            break;
        }
    }
    return values;
}

std::vector<double> split_json_number_array(const std::string &array_body)
{
    std::vector<double> values;
    std::istringstream stream(array_body);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (token == "null") {
            values.push_back(0.0);
            continue;
        }
        try {
            values.push_back(std::stod(token));
        } catch (...) {
            values.push_back(0.0);
        }
    }
    return values;
}

std::vector<int> split_json_int_array(const std::string &array_body)
{
    std::vector<int> values;
    std::istringstream stream(array_body);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (token == "null") {
            values.push_back(0);
            continue;
        }
        try {
            values.push_back(std::stoi(token));
        } catch (...) {
            values.push_back(0);
        }
    }
    return values;
}

std::string format_hour_label(const std::string &iso_time)
{
    if (iso_time.size() >= 16) {
        return iso_time.substr(11, 5);
    }
    return iso_time;
}

std::string format_day_label(const std::string &iso_date)
{
    if (iso_date.size() < 10) {
        return iso_date;
    }
    std::tm tm {};
    try {
        tm.tm_year = std::stoi(iso_date.substr(0, 4)) - 1900;
        tm.tm_mon = std::stoi(iso_date.substr(5, 2)) - 1;
        tm.tm_mday = std::stoi(iso_date.substr(8, 2));
    } catch (...) {
        return iso_date;
    }
    std::mktime(&tm);
    static const char *kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char *day_name =
        (tm.tm_wday >= 0 && tm.tm_wday <= 6) ? kDays[tm.tm_wday] : "";
    if (day_name[0] == '\0') {
        return iso_date;
    }
    return std::string(day_name) + " " + iso_date;
}

} // namespace

WeatherBackend::WeatherBackend(WeatherConfig config, EventWriter *events)
    : config_(std::move(config))
    , events_(events)
{
}

std::string WeatherBackend::describe_weather_code(int code)
{
    switch (code) {
    case 0:
        return "Clear sky";
    case 1:
        return "Mainly clear";
    case 2:
        return "Partly cloudy";
    case 3:
        return "Overcast";
    case 45:
    case 48:
        return "Fog";
    case 51:
    case 53:
    case 55:
        return "Drizzle";
    case 56:
    case 57:
        return "Freezing drizzle";
    case 61:
    case 63:
    case 65:
        return "Rain";
    case 66:
    case 67:
        return "Freezing rain";
    case 71:
    case 73:
    case 75:
        return "Snow";
    case 77:
        return "Snow grains";
    case 80:
    case 81:
    case 82:
        return "Rain showers";
    case 85:
    case 86:
        return "Snow showers";
    case 95:
        return "Thunderstorm";
    case 96:
    case 99:
        return "Thunderstorm with hail";
    default:
        return "Unknown conditions";
    }
}

std::string WeatherBackend::curl_fetch(const std::string &url) const
{
    const std::string cmd =
        "curl -fsS --max-time 20 -A \"" + std::string(kUserAgent) + "\" \"" + url + "\" 2>/dev/null";
    return run_command(cmd);
}

std::string WeatherBackend::build_forecast_url(double latitude, double longitude) const
{
    std::ostringstream out;
    out << config_.provider_url << "?latitude=" << latitude << "&longitude=" << longitude
        << "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,uv_index"
        << "&hourly=temperature_2m,weather_code,precipitation_probability,relative_humidity_2m,"
           "uv_index"
        << "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_"
           "max,uv_index_max"
        << "&forecast_days=" << config_.daily_limit << "&timezone=auto";
    if (config_.temperature_unit == "fahrenheit") {
        out << "&temperature_unit=fahrenheit";
    }
    return out.str();
}

bool WeatherBackend::resolve_coordinates(double &latitude, double &longitude,
                                        std::string &location_name)
{
    if (latitude != 0.0 || longitude != 0.0) {
        if (location_name.empty()) {
            std::ostringstream out;
            out << std::fixed << std::setprecision(2) << latitude << ", " << longitude;
            location_name = out.str();
        }
        return true;
    }

    if (config_.city_name.empty()) {
        return false;
    }

    const std::string url = config_.geocoding_url + "?name=" + url_encode(config_.city_name) +
                            "&count=1&language=en&format=json";
    const std::string response = curl_fetch(url);
    if (response.empty() || response.find("\"results\"") == std::string::npos) {
        return false;
    }

    const size_t results_pos = response.find("\"results\":[");
    if (results_pos == std::string::npos) {
        return false;
    }
    const std::string slice = response.substr(results_pos);
    const std::string result_lat = json_get_string(slice, "latitude");
    const std::string result_lon = json_get_string(slice, "longitude");
    if (result_lat.empty() || result_lon.empty()) {
        return false;
    }
    latitude = std::stod(result_lat);
    longitude = std::stod(result_lon);
    location_name = json_get_string(slice, "name");
    if (location_name.empty()) {
        location_name = config_.city_name;
    }
    return true;
}

std::string WeatherBackend::build_cache_from_api(const std::string &api_json, double latitude,
                                                 double longitude,
                                                 const std::string &location_name)
{
    const std::string current_block = find_json_object(api_json, "current");

    const std::string current_time = json_get_string(current_block, "time");
    const std::string current_temp = json_get_string(current_block, "temperature_2m");
    const std::string current_code = json_get_string(current_block, "weather_code");
    const std::string current_wind = json_get_string(current_block, "wind_speed_10m");
    const std::string current_humidity = json_get_string(current_block, "relative_humidity_2m");
    const std::string current_uv = json_get_string(current_block, "uv_index");

    const int current_code_int = current_code.empty() ? 0 : std::stoi(current_code);
    const double current_temp_val = current_temp.empty() ? 0.0 : std::stod(current_temp);
    const double current_wind_val = current_wind.empty() ? 0.0 : std::stod(current_wind);
    const double current_humidity_val =
        current_humidity.empty() ? 0.0 : std::stod(current_humidity);
    const double current_uv_val = current_uv.empty() ? 0.0 : std::stod(current_uv);

    const std::vector<std::string> hourly_times =
        split_json_string_array(extract_array_body(api_json, "hourly", "time"));
    const std::vector<double> hourly_temps =
        split_json_number_array(extract_array_body(api_json, "hourly", "temperature_2m"));
    const std::vector<int> hourly_codes =
        split_json_int_array(extract_array_body(api_json, "hourly", "weather_code"));
    const std::vector<double> hourly_precip =
        split_json_number_array(
            extract_array_body(api_json, "hourly", "precipitation_probability"));
    const std::vector<double> hourly_humidity =
        split_json_number_array(extract_array_body(api_json, "hourly", "relative_humidity_2m"));
    const std::vector<double> hourly_uv =
        split_json_number_array(extract_array_body(api_json, "hourly", "uv_index"));

    const std::vector<std::string> daily_times =
        split_json_string_array(extract_array_body(api_json, "daily", "time"));
    const std::vector<double> daily_max =
        split_json_number_array(extract_array_body(api_json, "daily", "temperature_2m_max"));
    const std::vector<double> daily_min =
        split_json_number_array(extract_array_body(api_json, "daily", "temperature_2m_min"));
    const std::vector<int> daily_codes =
        split_json_int_array(extract_array_body(api_json, "daily", "weather_code"));
    const std::vector<double> daily_precip =
        split_json_number_array(
            extract_array_body(api_json, "daily", "precipitation_probability_max"));
    const std::vector<double> daily_uv =
        split_json_number_array(extract_array_body(api_json, "daily", "uv_index_max"));

    double current_precip_val = 0.0;
    if (!hourly_precip.empty()) {
        current_precip_val = hourly_precip[0];
    }

    std::ostringstream out;
    out << "{\n"
        << "  \"fetched_at\": " << unix_now_sec() << ",\n"
        << "  \"location\": \"" << json_escape(location_name) << "\",\n"
        << "  \"latitude\": " << latitude << ",\n"
        << "  \"longitude\": " << longitude << ",\n"
        << "  \"temperature_unit\": \"" << json_escape(config_.temperature_unit) << "\",\n"
        << "  \"current\": {\n"
        << "    \"time\": \"" << json_escape(current_time) << "\",\n"
        << "    \"temperature\": " << current_temp_val << ",\n"
        << "    \"weather_code\": " << current_code_int << ",\n"
        << "    \"weather_description\": \"" << json_escape(describe_weather_code(current_code_int))
        << "\",\n"
        << "    \"wind_speed\": " << current_wind_val << ",\n"
        << "    \"relative_humidity\": " << current_humidity_val << ",\n"
        << "    \"uv_index\": " << current_uv_val << ",\n"
        << "    \"precipitation_probability\": " << current_precip_val << "\n"
        << "  },\n"
        << "  \"hourly\": [";

    const size_t hourly_count =
        std::min({hourly_times.size(), hourly_temps.size(), hourly_codes.size(),
                  static_cast<size_t>(config_.hourly_limit)});
    for (size_t i = 0; i < hourly_count; ++i) {
        if (i > 0) {
            out << ',';
        }
        const double precip = i < hourly_precip.size() ? hourly_precip[i] : 0.0;
        const double humidity = i < hourly_humidity.size() ? hourly_humidity[i] : 0.0;
        const double uv = i < hourly_uv.size() ? hourly_uv[i] : 0.0;
        out << "\n    {\"time\": \"" << json_escape(hourly_times[i]) << "\", \"label\": \""
            << json_escape(format_hour_label(hourly_times[i])) << "\", \"temperature\": "
            << hourly_temps[i] << ", \"weather_code\": " << hourly_codes[i]
            << ", \"weather_description\": \""
            << json_escape(describe_weather_code(hourly_codes[i]))
            << "\", \"precipitation_probability\": " << precip << ", \"relative_humidity\": "
            << humidity << ", \"uv_index\": " << uv << "}";
    }

    out << "\n  ],\n  \"daily\": [";
    const size_t daily_count = std::min({daily_times.size(), daily_max.size(), daily_min.size(),
                                         daily_codes.size(),
                                         static_cast<size_t>(config_.daily_limit)});
    for (size_t i = 0; i < daily_count; ++i) {
        if (i > 0) {
            out << ',';
        }
        const double precip = i < daily_precip.size() ? daily_precip[i] : 0.0;
        const double uv = i < daily_uv.size() ? daily_uv[i] : 0.0;
        out << "\n    {\"date\": \"" << json_escape(daily_times[i]) << "\", \"label\": \""
            << json_escape(format_day_label(daily_times[i])) << "\", \"temp_max\": " << daily_max[i]
            << ", \"temp_min\": " << daily_min[i] << ", \"weather_code\": " << daily_codes[i]
            << ", \"weather_description\": \""
            << json_escape(describe_weather_code(daily_codes[i]))
            << "\", \"precipitation_probability_max\": " << precip << ", \"uv_index_max\": " << uv
            << "}";
    }
    out << "\n  ]\n}\n";

    const std::string cache_json = out.str();
    save_cache(cache_json);
    evaluate_and_emit_alerts(cache_json);
    return cache_json;
}

bool WeatherBackend::save_config() const
{
    if (config_.config_path.empty()) {
        return false;
    }
    save_weather_config(config_.config_path, config_);
    return true;
}

bool WeatherBackend::save_cache(const std::string &cache_json) const
{
    const size_t slash = config_.cache_path.find_last_of('/');
    if (slash != std::string::npos) {
        ensure_directory(config_.cache_path.substr(0, slash));
    }
    const std::string temp_path = config_.cache_path + ".part";
    std::ofstream out(temp_path);
    if (!out.is_open()) {
        return false;
    }
    out << cache_json;
    out.close();
    return atomic_move_file(temp_path, config_.cache_path);
}

std::string WeatherBackend::load_cache_file() const
{
    if (!file_exists(config_.cache_path)) {
        return {};
    }
    std::ifstream in(config_.cache_path);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool WeatherBackend::cache_is_fresh(const std::string &cache_json) const
{
    return cache_age_sec(cache_json) <= config_.cache_ttl_sec;
}

uint64_t WeatherBackend::cache_age_sec(const std::string &cache_json) const
{
    const std::string fetched_at = json_get_string(cache_json, "fetched_at");
    if (fetched_at.empty()) {
        return UINT64_MAX;
    }
    const uint64_t fetched = static_cast<uint64_t>(std::stoull(fetched_at));
    const uint64_t now = unix_now_sec();
    return now > fetched ? now - fetched : 0;
}

std::string WeatherBackend::fetch()
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"weather disabled\"}";
    }

    double latitude = config_.latitude;
    double longitude = config_.longitude;
    std::string location_name = config_.city_name;
    if (!resolve_coordinates(latitude, longitude, location_name)) {
        return "{\"ok\":false,\"error\":\"location not configured. Set latitude/longitude or city_name in weather.conf\"}";
    }

    const std::string api_json = curl_fetch(build_forecast_url(latitude, longitude));
    if (api_json.empty() || api_json.find("\"current\"") == std::string::npos) {
        const std::string cached = load_cache_file();
        if (!cached.empty()) {
            return "{\"ok\":true,\"stale\":true,\"cache\":" + cached + "}";
        }
        return "{\"ok\":false,\"error\":\"forecast fetch failed\"}";
    }

    const std::string cache_json = build_cache_from_api(api_json, latitude, longitude, location_name);
    emit_weather_updated(cache_json);
    return "{\"ok\":true,\"stale\":false,\"cache\":" + cache_json + "}";
}

std::string WeatherBackend::set_location(const std::string &city_name)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"weather disabled\"}";
    }
    if (city_name.empty()) {
        return "{\"ok\":false,\"error\":\"city_name required\"}";
    }

    const std::string previous_city = config_.city_name;
    const double previous_lat = config_.latitude;
    const double previous_lon = config_.longitude;

    config_.city_name = city_name;
    config_.latitude = 0.0;
    config_.longitude = 0.0;

    double latitude = 0.0;
    double longitude = 0.0;
    std::string location_name = city_name;
    if (!resolve_coordinates(latitude, longitude, location_name)) {
        config_.city_name = previous_city;
        config_.latitude = previous_lat;
        config_.longitude = previous_lon;
        return "{\"ok\":false,\"error\":\"city not found\"}";
    }

    save_config();
    return fetch();
}

std::string WeatherBackend::alerts() const
{
    const std::string cached = load_cache_file();
    if (cached.empty()) {
        return "{\"ok\":true,\"alerts\":[]}";
    }
    return "{\"ok\":true,\"alerts\":" + build_alerts_json(cached) + "}";
}

void WeatherBackend::poll_refresh(uint64_t now_sec)
{
    if (!config_.enabled || config_.refresh_interval_sec == 0) {
        return;
    }
    if (last_poll_refresh_sec_ != 0 &&
        now_sec - last_poll_refresh_sec_ < config_.refresh_interval_sec) {
        return;
    }
    last_poll_refresh_sec_ = now_sec;

    const std::string cached = load_cache_file();
    if (!cached.empty() && cache_is_fresh(cached)) {
        return;
    }
    fetch();
}

std::string WeatherBackend::build_alerts_json(const std::string &cache_json) const
{
    if (!config_.alerts_enabled) {
        return "[]";
    }

    std::ostringstream out;
    out << '[';
    bool first = true;

    auto append_alert = [&](const std::string &type, const std::string &message) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << "\n  {\"type\":\"" << json_escape(type) << "\",\"message\":\""
            << json_escape(message) << "\"}";
    };

    const size_t current_key = cache_json.find("\"current\"");
    if (current_key != std::string::npos) {
        const size_t current_pos = cache_json.find('{', current_key);
        if (current_pos != std::string::npos) {
            const size_t current_end = cache_json.find('}', current_pos);
            const std::string current_block =
                cache_json.substr(current_pos, current_end - current_pos + 1);
            const std::string code_str = json_get_string(current_block, "weather_code");
            const int code = code_str.empty() ? 0 : std::stoi(code_str);
            const std::string wind_str = json_get_string(current_block, "wind_speed");
            const double wind = wind_str.empty() ? 0.0 : std::stod(wind_str);
            const std::string uv_str = json_get_string(current_block, "uv_index");
            const double uv = uv_str.empty() ? 0.0 : std::stod(uv_str);
            const std::string precip_str =
                json_get_string(current_block, "precipitation_probability");
            const double precip = precip_str.empty() ? 0.0 : std::stod(precip_str);

            if (code >= 95) {
                append_alert("thunderstorm", "Thunderstorm warning for current conditions");
            } else if (code >= 80) {
                append_alert("heavy_rain", "Heavy rain or showers expected now");
            }
            if (wind >= config_.alert_wind_threshold_kmh) {
                append_alert("high_wind", "High wind warning. Wind " + wind_str +
                                              " kilometers per hour");
            }
            if (precip >= static_cast<double>(config_.alert_precip_threshold_pct)) {
                append_alert("high_precip",
                             "High precipitation chance. " + precip_str + " percent");
            }
            if (uv >= static_cast<double>(config_.alert_uv_threshold)) {
                append_alert("high_uv", "High UV index. UV " + uv_str);
            }
        }
    }

    const size_t hourly_pos = cache_json.find("\"hourly\"");
    if (hourly_pos != std::string::npos) {
        const size_t array_start = cache_json.find('[', hourly_pos);
        const size_t hourly_end = cache_json.find(']', array_start);
        const std::string hourly_array =
            cache_json.substr(array_start + 1, hourly_end - array_start - 1);
        for (const auto &item : json_split_objects("[" + hourly_array + "]")) {
            const std::string code_str = json_get_string(item, "weather_code");
            const int code = code_str.empty() ? 0 : std::stoi(code_str);
            const std::string precip_str = json_get_string(item, "precipitation_probability");
            const double precip = precip_str.empty() ? 0.0 : std::stod(precip_str);
            const std::string label = json_get_string(item, "label");
            if (code >= 95) {
                append_alert("thunderstorm",
                             "Thunderstorm expected at " + (label.empty() ? "soon" : label));
                break;
            }
            if (precip >= static_cast<double>(config_.alert_precip_threshold_pct)) {
                append_alert("high_precip", "High rain chance at " +
                                                (label.empty() ? "soon" : label) + ". " +
                                                precip_str + " percent");
                break;
            }
        }
    }

    out << "\n]";
    return out.str();
}

void WeatherBackend::evaluate_and_emit_alerts(const std::string &cache_json)
{
    if (!config_.alerts_enabled || events_ == nullptr) {
        return;
    }

    const std::string alerts_json = build_alerts_json(cache_json);
    if (alerts_json == "[]" || alerts_json == "[\n]") {
        last_alert_signature_.clear();
        return;
    }

    if (alerts_json == last_alert_signature_) {
        return;
    }
    last_alert_signature_ = alerts_json;

    std::string message;
    for (const auto &item : json_split_objects(alerts_json)) {
        message = json_get_string(item, "message");
        if (!message.empty()) {
            break;
        }
    }
    if (message.empty()) {
        message = "Weather alert";
    }

    events_->emit("weather.alert",
                  "{\"message\":\"" + json_escape(message) + "\",\"alerts\":" + alerts_json + "}");
}

void WeatherBackend::emit_weather_updated(const std::string &cache_json)
{
    if (events_ == nullptr) {
        return;
    }
    const std::string location = json_get_string(cache_json, "location");
    events_->emit("weather.updated", "{\"location\":\"" + json_escape(location) + "\"}");
}

std::string WeatherBackend::read_cache() const
{
    const std::string cached = load_cache_file();
    if (cached.empty()) {
        return "{\"ok\":false,\"error\":\"no cached forecast\"}";
    }
    return "{\"ok\":true,\"fresh\":" + std::string(cache_is_fresh(cached) ? "true" : "false") +
           ",\"cache\":" + cached + "}";
}

std::string WeatherBackend::status() const
{
    const std::string cached = load_cache_file();
    if (cached.empty()) {
        return "{\"ok\":true,\"cached\":false}";
    }

    const std::string location = json_get_string(cached, "location");
    const size_t current_key = cached.find("\"current\"");
    std::string current_block;
    if (current_key != std::string::npos) {
        const size_t current_pos = cached.find('{', current_key);
        if (current_pos != std::string::npos) {
            const size_t current_end = cached.find('}', current_pos);
            if (current_end != std::string::npos) {
                current_block = cached.substr(current_pos, current_end - current_pos + 1);
            }
        }
    }
    const std::string current_temp = json_get_string(current_block, "temperature");
    const std::string current_desc = json_get_string(current_block, "weather_description");

    return "{\"ok\":true,\"cached\":true,\"fresh\":" +
           std::string(cache_is_fresh(cached) ? "true" : "false") +
           ",\"age_sec\":" + std::to_string(cache_age_sec(cached)) + ",\"location\":\"" +
           json_escape(location) + "\",\"temperature\":\"" + json_escape(current_temp) +
           "\",\"weather_description\":\"" + json_escape(current_desc) + "\"}";
}

} // namespace braillatron::connect
