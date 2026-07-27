#include "weather_backend.h"

#include "event_writer.h"
#include "json_utils.h"
#include "subprocess.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

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

bool is_americas_country(const std::string &country_code)
{
    if (country_code.size() != 2) {
        return false;
    }
    const char c0 = static_cast<char>(std::toupper(static_cast<unsigned char>(country_code[0])));
    const char c1 = static_cast<char>(std::toupper(static_cast<unsigned char>(country_code[1])));
    const std::string code = std::string(1, c0) + c1;
    return code == "US" || code == "CA" || code == "MX";
}

bool coords_in_americas(double latitude, double longitude)
{
    return latitude >= 14.0 && latitude <= 84.0 && longitude >= -168.0 && longitude <= -52.0;
}

std::string infer_temperature_unit(const std::string &country_code, double latitude, double longitude)
{
    if (is_americas_country(country_code)) {
        return "fahrenheit";
    }
    if (!country_code.empty()) {
        return "celsius";
    }
    return coords_in_americas(latitude, longitude) ? "fahrenheit" : "celsius";
}

std::string trim_copy(const std::string &value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string fold_key(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            out += static_cast<char>(std::tolower(ch));
        }
    }
    return out;
}

std::string lookup_alias(const std::string &key,
                         const std::vector<std::pair<const char *, const char *>> &table)
{
    const std::string folded = fold_key(key);
    for (const auto &entry : table) {
        if (folded == fold_key(entry.first)) {
            return entry.second;
        }
    }
    return {};
}

std::string normalize_country(const std::string &country)
{
    const std::string trimmed = trim_copy(country);
    if (trimmed.empty()) {
        return {};
    }

    static const std::vector<std::pair<const char *, const char *>> kCountries = {
        {"us", "United States"},
        {"usa", "United States"},
        {"u.s.", "United States"},
        {"u.s.a.", "United States"},
        {"america", "United States"},
        {"united states of america", "United States"},
        {"uk", "United Kingdom"},
        {"u.k.", "United Kingdom"},
        {"gb", "United Kingdom"},
        {"great britain", "United Kingdom"},
        {"england", "United Kingdom"},
        {"ca", "Canada"},
        {"can", "Canada"},
        {"au", "Australia"},
        {"aus", "Australia"},
        {"nz", "New Zealand"},
        {"de", "Germany"},
        {"fr", "France"},
        {"es", "Spain"},
        {"it", "Italy"},
        {"mx", "Mexico"},
        {"br", "Brazil"},
        {"in", "India"},
        {"jp", "Japan"},
    };

    const std::string matched = lookup_alias(trimmed, kCountries);
    if (!matched.empty()) {
        return matched;
    }
    return trimmed;
}

std::string normalize_country_code(const std::string &country)
{
    const std::string trimmed = trim_copy(country);
    if (trimmed.empty()) {
        return {};
    }

    if (trimmed.size() == 2 &&
        std::isalpha(static_cast<unsigned char>(trimmed[0])) &&
        std::isalpha(static_cast<unsigned char>(trimmed[1]))) {
        return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(trimmed[0])))) +
               std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(trimmed[1]))));
    }

    static const std::vector<std::pair<const char *, const char *>> kCountryCodes = {
        {"us", "US"},          {"usa", "US"},           {"u.s.", "US"},
        {"u.s.a.", "US"},      {"america", "US"},       {"united states", "US"},
        {"united states of america", "US"}, {"ca", "CA"}, {"can", "CA"},
        {"canada", "CA"},      {"uk", "GB"},            {"u.k.", "GB"},
        {"gb", "GB"},          {"great britain", "GB"}, {"england", "GB"},
        {"united kingdom", "GB"}, {"au", "AU"},         {"aus", "AU"},
        {"australia", "AU"},   {"nz", "NZ"},            {"new zealand", "NZ"},
        {"de", "DE"},          {"germany", "DE"},       {"fr", "FR"},
        {"france", "FR"},      {"es", "ES"},            {"spain", "ES"},
        {"it", "IT"},          {"italy", "IT"},         {"mx", "MX"},
        {"mexico", "MX"},      {"br", "BR"},            {"brazil", "BR"},
        {"in", "IN"},          {"india", "IN"},         {"jp", "JP"},
        {"japan", "JP"},
    };

    const std::string matched = lookup_alias(trimmed, kCountryCodes);
    if (!matched.empty()) {
        return matched;
    }

    const std::string folded = fold_key(normalize_country(trimmed));
    if (folded == "unitedstates") {
        return "US";
    }
    if (folded == "canada") {
        return "CA";
    }
    if (folded == "unitedkingdom") {
        return "GB";
    }
    if (folded == "australia") {
        return "AU";
    }
    if (folded == "newzealand") {
        return "NZ";
    }
    if (folded == "germany") {
        return "DE";
    }
    if (folded == "france") {
        return "FR";
    }
    if (folded == "spain") {
        return "ES";
    }
    if (folded == "italy") {
        return "IT";
    }
    if (folded == "mexico") {
        return "MX";
    }
    if (folded == "brazil") {
        return "BR";
    }
    if (folded == "india") {
        return "IN";
    }
    if (folded == "japan") {
        return "JP";
    }
    return {};
}

struct GeocodeHints {
    std::string city;
    std::string region;
    std::string country;
    std::string country_code;
};

std::string extract_json_array_body(const std::string &json, const std::string &key)
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
    if (start >= json.size() || json[start] != '[') {
        return {};
    }

    int depth = 0;
    bool in_string = false;
    bool escape = false;
    const size_t body_start = start + 1;
    for (size_t i = start; i < json.size(); ++i) {
        const char ch = json[i];
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '[') {
            ++depth;
        } else if (ch == ']') {
            --depth;
            if (depth == 0) {
                return json.substr(body_start, i - body_start);
            }
        }
    }
    return {};
}

std::string extract_city_search_name(const std::string &value)
{
    const std::string trimmed = trim_copy(value);
    const size_t comma = trimmed.find(',');
    if (comma == std::string::npos) {
        return trimmed;
    }
    return trim_copy(trimmed.substr(0, comma));
}

bool country_hint_is_us_or_ca(const std::string &country)
{
    const std::string normalized = normalize_country(country);
    const std::string folded = fold_key(normalized);
    return folded == "unitedstates" || folded == "canada" || fold_key(country) == "us" ||
           fold_key(country) == "usa" || fold_key(country) == "ca" || fold_key(country) == "can";
}

std::string normalize_region(const std::string &region, const std::string &country_hint)
{
    const std::string trimmed = trim_copy(region);
    if (trimmed.empty()) {
        return {};
    }

    static const std::vector<std::pair<const char *, const char *>> kUsStates = {
        {"al", "Alabama"},       {"alabama", "Alabama"},     {"ak", "Alaska"},
        {"alaska", "Alaska"},    {"az", "Arizona"},          {"arizona", "Arizona"},
        {"ar", "Arkansas"},      {"arkansas", "Arkansas"},   {"ca", "California"},
        {"calif", "California"}, {"cali", "California"},    {"california", "California"},
        {"co", "Colorado"},      {"colorado", "Colorado"},   {"ct", "Connecticut"},
        {"connecticut", "Connecticut"}, {"de", "Delaware"},    {"delaware", "Delaware"},
        {"fl", "Florida"},       {"fla", "Florida"},         {"florida", "Florida"},
        {"ga", "Georgia"},       {"georgia", "Georgia"},     {"hi", "Hawaii"},
        {"hawaii", "Hawaii"},    {"id", "Idaho"},            {"idaho", "Idaho"},
        {"il", "Illinois"},      {"ill", "Illinois"},        {"illinois", "Illinois"},
        {"in", "Indiana"},       {"ind", "Indiana"},         {"indiana", "Indiana"},
        {"ia", "Iowa"},          {"iowa", "Iowa"},           {"ks", "Kansas"},
        {"kansas", "Kansas"},    {"ky", "Kentucky"},         {"kentucky", "Kentucky"},
        {"la", "Louisiana"},     {"louisiana", "Louisiana"}, {"me", "Maine"},
        {"maine", "Maine"},      {"md", "Maryland"},         {"maryland", "Maryland"},
        {"ma", "Massachusetts"}, {"mass", "Massachusetts"},  {"massachusetts", "Massachusetts"},
        {"mi", "Michigan"},      {"mich", "Michigan"},       {"michigan", "Michigan"},
        {"mn", "Minnesota"},     {"minn", "Minnesota"},      {"minnesota", "Minnesota"},
        {"ms", "Mississippi"},   {"miss", "Mississippi"},    {"mississippi", "Mississippi"},
        {"mo", "Missouri"},      {"missouri", "Missouri"},   {"mt", "Montana"},
        {"montana", "Montana"},  {"ne", "Nebraska"},         {"nebraska", "Nebraska"},
        {"nv", "Nevada"},        {"nevada", "Nevada"},       {"nh", "New Hampshire"},
        {"new hampshire", "New Hampshire"}, {"nj", "New Jersey"}, {"new jersey", "New Jersey"},
        {"nm", "New Mexico"},    {"new mexico", "New Mexico"}, {"ny", "New York"},
        {"new york", "New York"}, {"nc", "North Carolina"},  {"north carolina", "North Carolina"},
        {"nd", "North Dakota"},  {"north dakota", "North Dakota"}, {"oh", "Ohio"},
        {"ohio", "Ohio"},        {"ok", "Oklahoma"},         {"oklahoma", "Oklahoma"},
        {"or", "Oregon"},        {"ore", "Oregon"},          {"oregon", "Oregon"},
        {"pa", "Pennsylvania"},  {"penn", "Pennsylvania"},   {"pennsylvania", "Pennsylvania"},
        {"ri", "Rhode Island"},  {"rhode island", "Rhode Island"}, {"sc", "South Carolina"},
        {"south carolina", "South Carolina"}, {"sd", "South Dakota"},
        {"south dakota", "South Dakota"}, {"tn", "Tennessee"}, {"tenn", "Tennessee"},
        {"tennessee", "Tennessee"}, {"tx", "Texas"},          {"tex", "Texas"},
        {"texas", "Texas"},      {"ut", "Utah"},             {"utah", "Utah"},
        {"vt", "Vermont"},       {"vermont", "Vermont"},     {"va", "Virginia"},
        {"virginia", "Virginia"}, {"wa", "Washington"},      {"wash", "Washington"},
        {"washington", "Washington"}, {"wv", "West Virginia"}, {"west virginia", "West Virginia"},
        {"wi", "Wisconsin"},     {"wis", "Wisconsin"},       {"wisconsin", "Wisconsin"},
        {"wy", "Wyoming"},       {"wyoming", "Wyoming"},     {"dc", "District of Columbia"},
        {"d.c.", "District of Columbia"},
    };

    static const std::vector<std::pair<const char *, const char *>> kCaProvinces = {
        {"ab", "Alberta"},       {"alberta", "Alberta"},     {"bc", "British Columbia"},
        {"b.c.", "British Columbia"}, {"british columbia", "British Columbia"},
        {"mb", "Manitoba"},      {"manitoba", "Manitoba"},   {"nb", "New Brunswick"},
        {"new brunswick", "New Brunswick"}, {"nl", "Newfoundland and Labrador"},
        {"newfoundland", "Newfoundland and Labrador"},
        {"ns", "Nova Scotia"},   {"nova scotia", "Nova Scotia"}, {"nt", "Northwest Territories"},
        {"nu", "Nunavut"},       {"on", "Ontario"},          {"ont", "Ontario"},
        {"ontario", "Ontario"},  {"pe", "Prince Edward Island"},
        {"prince edward island", "Prince Edward Island"}, {"qc", "Quebec"},
        {"que", "Quebec"},       {"quebec", "Quebec"},       {"sk", "Saskatchewan"},
        {"sask", "Saskatchewan"}, {"saskatchewan", "Saskatchewan"}, {"yt", "Yukon"},
        {"yukon", "Yukon"},
    };

    const bool prefer_ca = fold_key(country_hint) == "ca" || fold_key(country_hint) == "can" ||
                           fold_key(country_hint) == "canada";
    const bool prefer_us = country_hint.empty() || country_hint_is_us_or_ca(country_hint);

    if (prefer_ca) {
        const std::string matched = lookup_alias(trimmed, kCaProvinces);
        if (!matched.empty()) {
            return matched;
        }
    }
    if (prefer_us) {
        const std::string matched = lookup_alias(trimmed, kUsStates);
        if (!matched.empty()) {
            return matched;
        }
    }
    {
        const std::string matched = lookup_alias(trimmed, kCaProvinces);
        if (!matched.empty()) {
            return matched;
        }
    }
    {
        const std::string matched = lookup_alias(trimmed, kUsStates);
        if (!matched.empty()) {
            return matched;
        }
    }
    return trimmed;
}

bool region_matches_hint(const std::string &admin1, const std::string &region_hint,
                         const std::string &country_hint)
{
    if (admin1.empty() || region_hint.empty()) {
        return false;
    }

    const std::string folded_admin = fold_key(admin1);
    const std::string folded_hint = fold_key(region_hint);
    if (folded_admin == folded_hint) {
        return true;
    }

    const std::string normalized_hint = normalize_region(region_hint, country_hint);
    if (!normalized_hint.empty() && folded_admin == fold_key(normalized_hint)) {
        return true;
    }

    const std::string normalized_admin = normalize_region(admin1, country_hint);
    return !normalized_admin.empty() && fold_key(normalized_admin) == folded_hint;
}

struct GeocodeCandidate {
    std::string name;
    std::string admin1;
    std::string country;
    std::string country_code;
    double latitude = 0.0;
    double longitude = 0.0;
    int population = 0;
};

int score_geocode_candidate(const GeocodeCandidate &candidate, const GeocodeHints &hints)
{
    int score = 0;
    if (!hints.city.empty() && fold_key(candidate.name) == fold_key(hints.city)) {
        score += 10;
    }
    if (!hints.country_code.empty() &&
        fold_key(candidate.country_code) == fold_key(hints.country_code)) {
        score += 100;
    }
    if (!hints.country.empty() && fold_key(candidate.country) == fold_key(hints.country)) {
        score += 50;
    }
    if (!hints.region.empty() &&
        region_matches_hint(candidate.admin1, hints.region, hints.country)) {
        score += 200;
    }
    return score;
}

bool pick_geocode_candidate(const std::vector<GeocodeCandidate> &candidates,
                            const GeocodeHints &hints, GeocodeCandidate &selected)
{
    if (candidates.empty()) {
        return false;
    }

    if (hints.region.empty() && hints.country.empty() && hints.country_code.empty()) {
        selected = candidates.front();
        return true;
    }

    int best_score = -1;
    int best_population = -1;
    size_t best_index = 0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const int score = score_geocode_candidate(candidates[i], hints);
        if (score > best_score ||
            (score == best_score && candidates[i].population > best_population)) {
            best_score = score;
            best_population = candidates[i].population;
            best_index = i;
        }
    }

    if (best_score <= 0) {
        return false;
    }

    selected = candidates[best_index];
    return true;
}

std::string format_resolved_location_name(const GeocodeCandidate &candidate)
{
    if (candidate.admin1.empty()) {
        return candidate.name;
    }
    return candidate.name + ", " + candidate.admin1;
}

GeocodeHints build_geocode_hints(const std::string &city, const std::string &region,
                                 const std::string &country)
{
    GeocodeHints hints;
    hints.city = trim_copy(city);
    hints.country = normalize_country(country);
    hints.country_code = normalize_country_code(country);
    hints.region =
        normalize_region(region, hints.country.empty() ? country : hints.country);
    return hints;
}

std::string build_geocode_query(const std::string &city, const std::string &region,
                                const std::string &country)
{
    const GeocodeHints hints = build_geocode_hints(city, region, country);
    if (hints.city.empty()) {
        return {};
    }

    std::ostringstream out;
    out << hints.city;
    if (!hints.region.empty()) {
        out << ", " << hints.region;
    }
    if (!hints.country.empty()) {
        out << ", " << hints.country;
    }
    return out.str();
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

std::string WeatherBackend::slot_cache_path(size_t slot) const
{
    static const char kSlotLetters[] = {'a', 'b', 'c'};
    if (slot >= WeatherConfig::kMaxCities) {
        slot = 0;
    }
    if (!config_.cache_dir.empty()) {
        return config_.cache_dir + "/cache_" + kSlotLetters[slot] + ".json";
    }
    if (slot == 0 && !config_.cache_path.empty()) {
        return config_.cache_path;
    }
    return "/data/braillatron/weather/cache_" + std::string(1, kSlotLetters[slot]) + ".json";
}

void WeatherBackend::sync_legacy_fields_from_active()
{
    const WeatherCitySlot &active = config_.cities[config_.active_slot];
    config_.city_name = active.city_name;
    config_.latitude = active.latitude;
    config_.longitude = active.longitude;
}

std::string WeatherBackend::build_forecast_url(double latitude, double longitude,
                                               const std::string &temperature_unit) const
{
    std::ostringstream out;
    out << config_.provider_url << "?latitude=" << latitude << "&longitude=" << longitude
        << "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,uv_index"
        << "&hourly=temperature_2m,weather_code,precipitation_probability,relative_humidity_2m,"
           "uv_index"
        << "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_"
           "max,uv_index_max"
        << "&forecast_days=" << config_.daily_limit << "&timezone=auto";
    if (temperature_unit == "fahrenheit") {
        out << "&temperature_unit=fahrenheit";
    }
    return out.str();
}

bool WeatherBackend::resolve_coordinates(WeatherCitySlot &city, std::string &location_name,
                                         std::string &country_code,
                                         const std::string &region_hint,
                                         const std::string &country_hint)
{
    double latitude = city.latitude;
    double longitude = city.longitude;
    if (latitude != 0.0 || longitude != 0.0) {
        if (location_name.empty()) {
            std::ostringstream out;
            out << std::fixed << std::setprecision(2) << latitude << ", " << longitude;
            location_name = out.str();
        }
        return true;
    }

    const std::string search_city = extract_city_search_name(city.city_name);
    if (search_city.empty()) {
        return false;
    }

    GeocodeHints hints = build_geocode_hints(search_city, region_hint, country_hint);
    if (!country_code.empty() && hints.country_code.empty()) {
        hints.country_code = normalize_country_code(country_code);
    }

    std::ostringstream url;
    url << config_.geocoding_url << "?name=" << url_encode(search_city)
        << "&count=10&language=en&format=json";
    if (!hints.country_code.empty()) {
        url << "&countryCode=" << url_encode(hints.country_code);
    }

    const std::string response = curl_fetch(url.str());
    if (response.empty() || response.find("\"results\"") == std::string::npos) {
        return false;
    }

    const std::string results_array = extract_json_array_body(response, "results");
    if (results_array.empty()) {
        return false;
    }

    std::vector<GeocodeCandidate> candidates;
    for (const std::string &object : json_split_objects("[" + results_array + "]")) {
        const std::string result_lat = json_get_string(object, "latitude");
        const std::string result_lon = json_get_string(object, "longitude");
        if (result_lat.empty() || result_lon.empty()) {
            continue;
        }

        GeocodeCandidate candidate;
        candidate.name = json_get_string(object, "name");
        candidate.admin1 = json_get_string(object, "admin1");
        candidate.country = json_get_string(object, "country");
        candidate.country_code = json_get_string(object, "country_code");
        try {
            candidate.latitude = std::stod(result_lat);
            candidate.longitude = std::stod(result_lon);
        } catch (...) {
            continue;
        }
        const std::string population = json_get_string(object, "population");
        if (!population.empty()) {
            try {
                candidate.population = std::stoi(population);
            } catch (...) {
                candidate.population = 0;
            }
        }
        candidates.push_back(std::move(candidate));
    }

    if (candidates.empty()) {
        return false;
    }

    GeocodeCandidate selected;
    if (!pick_geocode_candidate(candidates, hints, selected)) {
        return false;
    }

    latitude = selected.latitude;
    longitude = selected.longitude;
    location_name = format_resolved_location_name(selected);
    country_code = selected.country_code;
    city.latitude = latitude;
    city.longitude = longitude;
    return true;
}

std::string WeatherBackend::effective_temperature_unit_for(const std::string &country_code,
                                                           double latitude,
                                                           double longitude) const
{
    if (config_.temperature_unit != "auto" && !config_.temperature_unit.empty()) {
        return config_.temperature_unit;
    }
    return infer_temperature_unit(country_code, latitude, longitude);
}

std::string WeatherBackend::build_cache_from_api(const std::string &api_json, double latitude,
                                                 double longitude,
                                                 const std::string &location_name, size_t slot)
{
    if (slot >= WeatherConfig::kMaxCities) {
        slot = 0;
    }

    const WeatherCitySlot &city = config_.cities[slot];
    const std::string temperature_unit = effective_temperature_unit_for(
        resolved_country_codes_[slot], city.latitude, city.longitude);

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
        << "  \"slot\": " << slot << ",\n"
        << "  \"fetched_at\": " << unix_now_sec() << ",\n"
        << "  \"location\": \"" << json_escape(location_name) << "\",\n"
        << "  \"latitude\": " << latitude << ",\n"
        << "  \"longitude\": " << longitude << ",\n"
        << "  \"temperature_unit\": \"" << json_escape(temperature_unit) << "\",\n"
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
    save_cache(cache_json, slot);
    if (slot == config_.active_slot) {
        evaluate_and_emit_alerts(cache_json);
    }
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

bool WeatherBackend::save_cache(const std::string &cache_json, size_t slot) const
{
    const std::string cache_path = slot_cache_path(slot);
    const size_t slash = cache_path.find_last_of('/');
    if (slash != std::string::npos) {
        ensure_directory(cache_path.substr(0, slash));
    }
    const std::string temp_path = cache_path + ".part";
    std::ofstream out(temp_path);
    if (!out.is_open()) {
        return false;
    }
    out << cache_json;
    out.close();
    return atomic_move_file(temp_path, cache_path);
}

std::string WeatherBackend::load_cache_file(size_t slot) const
{
    const std::string cache_path = slot_cache_path(slot);
    if (!file_exists(cache_path)) {
        return {};
    }
    std::ifstream in(cache_path);
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

std::string WeatherBackend::fetch_slot(size_t slot)
{
    if (slot >= WeatherConfig::kMaxCities) {
        return "{\"ok\":false,\"error\":\"invalid city slot\"}";
    }

    WeatherCitySlot &city = config_.cities[slot];
    std::string location_name = city.city_name;
    std::string country_code = resolved_country_codes_[slot];
    if (!resolve_coordinates(city, location_name, country_code)) {
        return "{\"ok\":false,\"error\":\"location not configured for slot\"}";
    }
    resolved_country_codes_[slot] = country_code;
    sync_legacy_fields_from_active();
    save_config();

    const std::string temperature_unit =
        effective_temperature_unit_for(country_code, city.latitude, city.longitude);
    const std::string api_json =
        curl_fetch(build_forecast_url(city.latitude, city.longitude, temperature_unit));
    if (api_json.empty() || api_json.find("\"current\"") == std::string::npos) {
        const std::string cached = load_cache_file(slot);
        if (!cached.empty()) {
            return "{\"ok\":true,\"stale\":true,\"slot\":" + std::to_string(slot) +
                   ",\"cache\":" + cached + "}";
        }
        return "{\"ok\":false,\"error\":\"forecast fetch failed\"}";
    }

    const std::string cache_json =
        build_cache_from_api(api_json, city.latitude, city.longitude, location_name, slot);
    if (slot == config_.active_slot) {
        emit_weather_updated(cache_json);
    }
    return "{\"ok\":true,\"stale\":false,\"slot\":" + std::to_string(slot) +
           ",\"cache\":" + cache_json + "}";
}

std::string WeatherBackend::fetch()
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"weather disabled\"}";
    }
    return fetch_slot(config_.active_slot);
}

std::string WeatherBackend::list_cities() const
{
    static const char *kLabels[] = {"City A", "City B", "City C"};
    std::ostringstream out;
    out << "{\"ok\":true,\"active_slot\":" << config_.active_slot << ",\"cities\":[";
    for (size_t i = 0; i < WeatherConfig::kMaxCities; ++i) {
        if (i > 0) {
            out << ',';
        }
        const WeatherCitySlot &city = config_.cities[i];
        const std::string cache = load_cache_file(i);
        std::string summary;
        if (!cache.empty()) {
            const std::string location = json_get_string(cache, "location");
            const size_t current_key = cache.find("\"current\"");
            if (current_key != std::string::npos) {
                const size_t current_pos = cache.find('{', current_key);
                if (current_pos != std::string::npos) {
                    const size_t current_end = cache.find('}', current_pos);
                    const std::string current_block =
                        cache.substr(current_pos, current_end - current_pos + 1);
                    const std::string temp = json_get_string(current_block, "temperature");
                    const std::string desc =
                        json_get_string(current_block, "weather_description");
                    if (!location.empty()) {
                        summary = location;
                    }
                    if (!temp.empty()) {
                        if (!summary.empty()) {
                            summary += ": ";
                        }
                        summary += temp;
                        const std::string unit = json_get_string(cache, "temperature_unit");
                        summary += (unit == "fahrenheit") ? " F" : " C";
                    }
                    if (!desc.empty()) {
                        summary += " " + desc;
                    }
                }
            }
        }

        out << "\n  {\"slot\":" << i << ",\"label\":\"" << kLabels[i] << "\",\"name\":\""
            << json_escape(city.city_name) << "\",\"configured\":"
            << (city.city_name.empty() ? "false" : "true") << ",\"summary\":\""
            << json_escape(summary) << "\"}";
    }
    out << "\n]}";
    return out.str();
}

std::string WeatherBackend::select_city(size_t slot)
{
    if (slot >= WeatherConfig::kMaxCities) {
        return "{\"ok\":false,\"error\":\"invalid city slot\"}";
    }
    config_.active_slot = slot;
    sync_legacy_fields_from_active();
    save_config();
    const std::string cached = load_cache_file(slot);
    if (cached.empty()) {
        return "{\"ok\":true,\"active_slot\":" + std::to_string(slot) + ",\"cached\":false}";
    }
    return "{\"ok\":true,\"active_slot\":" + std::to_string(slot) +
           ",\"fresh\":" + std::string(cache_is_fresh(cached) ? "true" : "false") +
           ",\"cache\":" + cached + "}";
}

std::string WeatherBackend::set_city(size_t slot, const std::string &city_name,
                                     const std::string &region, const std::string &country)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"weather disabled\"}";
    }
    if (slot >= WeatherConfig::kMaxCities) {
        return "{\"ok\":false,\"error\":\"invalid city slot\"}";
    }
    if (city_name.empty()) {
        return "{\"ok\":false,\"error\":\"city_name required\"}";
    }

    const GeocodeHints hints = build_geocode_hints(city_name, region, country);
    if (hints.city.empty()) {
        return "{\"ok\":false,\"error\":\"city_name required\"}";
    }

    WeatherCitySlot previous = config_.cities[slot];
    std::string previous_country = resolved_country_codes_[slot];

    config_.cities[slot].city_name = hints.city;
    config_.cities[slot].latitude = 0.0;
    config_.cities[slot].longitude = 0.0;
    resolved_country_codes_[slot].clear();

    WeatherCitySlot &city = config_.cities[slot];
    std::string location_name = build_geocode_query(city_name, region, country);
    std::string country_code;
    if (!resolve_coordinates(city, location_name, country_code, hints.region, country)) {
        config_.cities[slot] = previous;
        resolved_country_codes_[slot] = previous_country;
        return "{\"ok\":false,\"error\":\"city not found\"}";
    }

    config_.cities[slot].city_name = location_name;
    resolved_country_codes_[slot] = country_code;
    config_.active_slot = slot;
    sync_legacy_fields_from_active();
    save_config();
    return fetch_slot(slot);
}

std::string WeatherBackend::set_location(const std::string &city_name)
{
    return set_city(config_.active_slot, city_name);
}

std::string WeatherBackend::detect_ip_location() const
{
    const std::string url =
        "http://ip-api.com/json/?fields=status,message,country,countryCode,regionName,city,lat,lon";
    const std::string response = curl_fetch(url);
    if (response.empty()) {
        return "{\"ok\":false,\"error\":\"ip location lookup failed\"}";
    }

    const std::string status = json_get_string(response, "status");
    if (status != "success") {
        const std::string message = json_get_string(response, "message");
        return "{\"ok\":false,\"error\":\"" + json_escape(message.empty() ? "ip location failed"
                                                                         : message) +
               "\"}";
    }

    const std::string city = json_get_string(response, "city");
    const std::string region = json_get_string(response, "regionName");
    const std::string country = json_get_string(response, "country");
    const std::string country_code = json_get_string(response, "countryCode");
    const std::string lat = json_get_string(response, "lat");
    const std::string lon = json_get_string(response, "lon");
    if (city.empty() || lat.empty() || lon.empty()) {
        return "{\"ok\":false,\"error\":\"ip location incomplete\"}";
    }

    std::ostringstream location_name;
    location_name << city;
    if (!region.empty()) {
        location_name << ", " << region;
    }
    if (!country.empty()) {
        location_name << ", " << country;
    }

    return "{\"ok\":true,\"city\":\"" + json_escape(city) + "\",\"region\":\"" +
           json_escape(region) + "\",\"country\":\"" + json_escape(country) +
           "\",\"country_code\":\"" + json_escape(country_code) + "\",\"latitude\":\"" +
           json_escape(lat) + "\",\"longitude\":\"" + json_escape(lon) + "\",\"location\":\"" +
           json_escape(location_name.str()) + "\"}";
}

std::string WeatherBackend::set_city_from_ip(size_t slot)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"weather disabled\"}";
    }
    if (slot >= WeatherConfig::kMaxCities) {
        return "{\"ok\":false,\"error\":\"invalid city slot\"}";
    }

    const std::string detected = detect_ip_location();
    if (!json_get_bool(detected, "ok", false)) {
        return detected;
    }

    WeatherCitySlot previous = config_.cities[slot];
    std::string previous_country = resolved_country_codes_[slot];

    const std::string location_name = json_get_string(detected, "location");
    const std::string country_code = json_get_string(detected, "country_code");
    const double latitude = std::stod(json_get_string(detected, "latitude"));
    const double longitude = std::stod(json_get_string(detected, "longitude"));

    config_.cities[slot].city_name = location_name.empty() ? json_get_string(detected, "city")
                                                           : location_name;
    config_.cities[slot].latitude = latitude;
    config_.cities[slot].longitude = longitude;
    resolved_country_codes_[slot] = country_code;

    const std::string fetched = fetch_slot(slot);
    if (!json_get_bool(fetched, "ok", false)) {
        config_.cities[slot] = previous;
        resolved_country_codes_[slot] = previous_country;
        return fetched;
    }
    return fetched;
}

std::string WeatherBackend::set_temperature_unit(const std::string &unit)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"weather disabled\"}";
    }
    if (unit != "celsius" && unit != "fahrenheit" && unit != "auto") {
        return "{\"ok\":false,\"error\":\"temperature_unit must be celsius, fahrenheit, or auto\"}";
    }

    config_.temperature_unit = unit;
    save_config();
    return fetch();
}

std::string WeatherBackend::config_status() const
{
    const WeatherCitySlot &active = config_.cities[config_.active_slot];
    const std::string effective = effective_temperature_unit_for(
        resolved_country_codes_[config_.active_slot], active.latitude, active.longitude);
    return "{\"ok\":true,\"temperature_unit\":\"" + json_escape(config_.temperature_unit) +
           "\",\"effective_temperature_unit\":\"" + json_escape(effective) + "\"}";
}

std::string WeatherBackend::alerts() const
{
    const std::string cached = load_cache_file(config_.active_slot);
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

    const std::string cached = load_cache_file(config_.active_slot);
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
        out << "{\"type\":\"" << json_escape(type) << "\",\"message\":\""
            << json_escape(message) << "\"}";
    };

    const std::string location = json_get_string(cache_json, "location");
    const std::string area = location.empty() ? std::string("your area") : location;

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
                append_alert("thunderstorm",
                             "Thunderstorm warning for " + area);
            } else if (code >= 80) {
                append_alert("heavy_rain",
                             "Heavy rain or showers expected now in " + area);
            }
            if (wind >= config_.alert_wind_threshold_kmh) {
                append_alert("high_wind",
                             "High wind warning for " + area + ". Wind " + wind_str +
                                 " kilometers per hour");
            }
            if (precip >= static_cast<double>(config_.alert_precip_threshold_pct)) {
                append_alert("high_precip",
                             "High precipitation chance for " + area + ". " + precip_str +
                                 " percent");
            }
            if (uv >= static_cast<double>(config_.alert_uv_threshold)) {
                append_alert("high_uv",
                             "High UV index for " + area + ". UV " + uv_str);
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
                             "Thunderstorm expected at " +
                                 (label.empty() ? "soon" : label) + " for " + area);
                break;
            }
            if (precip >= static_cast<double>(config_.alert_precip_threshold_pct)) {
                append_alert("high_precip",
                             "High rain chance at " + (label.empty() ? "soon" : label) +
                                 " for " + area + ". " + precip_str + " percent");
                break;
            }
        }
    }

    out << ']';
    return out.str();
}

void WeatherBackend::evaluate_and_emit_alerts(const std::string &cache_json)
{
    if (!config_.alerts_enabled || events_ == nullptr) {
        return;
    }

    const std::string alerts_json = build_alerts_json(cache_json);
    if (alerts_json == "[]") {
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
    const std::string cached = load_cache_file(config_.active_slot);
    if (cached.empty()) {
        return "{\"ok\":false,\"error\":\"no cached forecast\"}";
    }
    return "{\"ok\":true,\"fresh\":" + std::string(cache_is_fresh(cached) ? "true" : "false") +
           ",\"active_slot\":" + std::to_string(config_.active_slot) + ",\"cache\":" + cached +
           "}";
}

std::string WeatherBackend::status() const
{
    const std::string cached = load_cache_file(config_.active_slot);
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
