#include "radio_backend.h"

#include "json_utils.h"
#include "subprocess.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace braillatron::connect {

namespace {

constexpr const char *kUserAgent = "Braillatron/1.0 (accessibility device)";

std::string trim_line(const std::string &value)
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

std::string lower_ascii(const std::string &value)
{
    std::string out = value;
    for (char &ch : out) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return out;
}

} // namespace

std::string RadioBackend::parse_icy_title(const std::string &headers)
{
    std::istringstream stream(headers);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim_line(line);
        const std::string lower = lower_ascii(line);
        if (lower.rfind("icy-name:", 0) == 0) {
            return trim_line(line.substr(9));
        }
        if (lower.rfind("icy-title:", 0) == 0) {
            return trim_line(line.substr(10));
        }
        if (lower.rfind("streamtitle=", 0) != std::string::npos) {
            const size_t eq = line.find('=');
            if (eq != std::string::npos && eq + 1 < line.size()) {
                std::string title = line.substr(eq + 1);
                if (!title.empty() && title.front() == '\'') {
                    title.erase(0, 1);
                }
                if (!title.empty() && title.back() == ';') {
                    title.pop_back();
                }
                if (!title.empty() && title.back() == '\'') {
                    title.pop_back();
                }
                return trim_line(title);
            }
        }
    }
    return {};
}

std::vector<RadioStation> RadioBackend::parse_stations_json(const std::string &json)
{
    std::vector<RadioStation> stations;
    const std::string stations_array = json_get_array_body(json, "stations");
    if (stations_array.empty()) {
        return stations;
    }

    for (const std::string &obj : json_split_objects("[" + stations_array + "]")) {
        RadioStation station;
        station.id = json_get_string(obj, "id");
        station.name = json_get_string(obj, "name");
        station.url = json_get_string(obj, "url");
        station.country = json_get_string(obj, "country");
        station.tags = json_get_string(obj, "tags");
        station.favorite = json_get_bool(obj, "favorite", false);
        if (!station.id.empty() && !station.name.empty() && !station.url.empty()) {
            stations.push_back(std::move(station));
        }
    }
    return stations;
}

RadioBackend::RadioBackend(RadioConfig config, MpvService *mpv, EventWriter *events)
    : config_(std::move(config))
    , mpv_(mpv)
    , events_(events)
{
    load_bundled();
    load_favorites();
}

std::string RadioBackend::curl_fetch(const std::string &url) const
{
    const std::string cmd = "curl -fsS --max-time 20 -A \"" + std::string(kUserAgent) + "\" \"" +
                            url + "\" 2>/dev/null";
    return run_command(cmd);
}

std::string RadioBackend::curl_headers(const std::string &url) const
{
    const std::string cmd = "curl -fsSI --max-time 10 -A \"" + std::string(kUserAgent) + "\" \"" +
                            url + "\" 2>/dev/null";
    return run_command(cmd);
}

bool RadioBackend::load_bundled()
{
    bundled_.clear();
    if (!file_exists(config_.stations_path)) {
        return true;
    }
    std::ifstream in(config_.stations_path);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    bundled_ = parse_stations_json(buffer.str());
    return true;
}

bool RadioBackend::load_favorites()
{
    favorites_.clear();
    if (!file_exists(config_.favorites_path)) {
        return true;
    }
    std::ifstream in(config_.favorites_path);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    favorites_ = parse_stations_json(buffer.str());
    for (RadioStation &station : favorites_) {
        station.favorite = true;
    }
    return true;
}

bool RadioBackend::save_favorites() const
{
    const size_t slash = config_.favorites_path.find_last_of('/');
    if (slash != std::string::npos) {
        ensure_directory(config_.favorites_path.substr(0, slash));
    }

    std::ostringstream out;
    out << "{\n  \"stations\": [\n";
    for (size_t i = 0; i < favorites_.size(); ++i) {
        if (i > 0) {
            out << ",\n";
        }
        const RadioStation &station = favorites_[i];
        out << "    {\"id\": \"" << json_escape(station.id) << "\", \"name\": \""
            << json_escape(station.name) << "\", \"url\": \"" << json_escape(station.url)
            << "\", \"country\": \"" << json_escape(station.country) << "\", \"tags\": \""
            << json_escape(station.tags) << "\", \"favorite\": true}";
    }
    out << "\n  ]\n}\n";

    const std::string temp_path = config_.favorites_path + ".part";
    std::ofstream file(temp_path);
    if (!file.is_open()) {
        return false;
    }
    file << out.str();
    file.close();
    return atomic_move_file(temp_path, config_.favorites_path);
}

std::string RadioBackend::merge_station_lists() const
{
    std::ostringstream out;
    out << "{\"ok\":true,\"stations\":[";
    bool first = true;

    auto emit = [&](const RadioStation &station) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << "{\"id\":\"" << json_escape(station.id) << "\",\"name\":\""
            << json_escape(station.name) << "\",\"url\":\"" << json_escape(station.url)
            << "\",\"country\":\"" << json_escape(station.country) << "\",\"tags\":\""
            << json_escape(station.tags) << "\",\"favorite\":"
            << (station.favorite ? "true" : "false") << "}";
    };

    for (const RadioStation &station : favorites_) {
        emit(station);
    }
    for (const RadioStation &station : bundled_) {
        bool skip = false;
        for (const RadioStation &fav : favorites_) {
            if (fav.id == station.id) {
                skip = true;
                break;
            }
        }
        if (!skip) {
            emit(station);
        }
    }
    for (const RadioStation &station : search_results_) {
        bool skip = false;
        for (const RadioStation &existing : favorites_) {
            if (existing.id == station.id) {
                skip = true;
                break;
            }
        }
        if (!skip) {
            for (const RadioStation &existing : bundled_) {
                if (existing.id == station.id) {
                    skip = true;
                    break;
                }
            }
        }
        if (!skip) {
            emit(station);
        }
    }

    out << "]}";
    return out.str();
}

std::string RadioBackend::list_stations()
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"radio disabled\"}";
    }
    load_bundled();
    load_favorites();
    return merge_station_lists();
}

std::string RadioBackend::search(const std::string &query)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"radio disabled\"}";
    }
    if (query.empty()) {
        return "{\"ok\":false,\"error\":\"empty query\"}";
    }

    std::string encoded;
    for (unsigned char ch : query) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
            encoded += static_cast<char>(ch);
        } else if (ch == ' ') {
            encoded += "%20";
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", ch);
            encoded += buf;
        }
    }

    const std::string url = config_.radio_browser_url + "/json/stations/search?name=" + encoded +
                            "&country=" + config_.default_country + "&limit=" +
                            std::to_string(config_.search_limit);
    const std::string response = curl_fetch(url);
    if (response.empty() || response.front() != '[') {
        return "{\"ok\":false,\"error\":\"search failed\"}";
    }

    search_results_.clear();
    for (const std::string &obj : json_split_objects(response)) {
        RadioStation station;
        station.name = json_get_string(obj, "name");
        station.url = json_get_string(obj, "url");
        station.country = json_get_string(obj, "country");
        station.tags = json_get_string(obj, "tags");
        const std::string stationuuid = json_get_string(obj, "stationuuid");
        station.id = stationuuid.empty() ? ("search-" + std::to_string(search_results_.size()))
                                         : stationuuid;
        if (!station.name.empty() && !station.url.empty()) {
            search_results_.push_back(std::move(station));
        }
    }

    return merge_station_lists();
}

const RadioStation *RadioBackend::find_station(const std::string &station_id) const
{
    for (const RadioStation &station : favorites_) {
        if (station.id == station_id) {
            return &station;
        }
    }
    for (const RadioStation &station : bundled_) {
        if (station.id == station_id) {
            return &station;
        }
    }
    for (const RadioStation &station : search_results_) {
        if (station.id == station_id) {
            return &station;
        }
    }
    return nullptr;
}

RadioStation *RadioBackend::find_station_mut(const std::string &station_id)
{
    for (RadioStation &station : favorites_) {
        if (station.id == station_id) {
            return &station;
        }
    }
    for (RadioStation &station : bundled_) {
        if (station.id == station_id) {
            return &station;
        }
    }
    for (RadioStation &station : search_results_) {
        if (station.id == station_id) {
            return &station;
        }
    }
    return nullptr;
}

std::string RadioBackend::station_event_json(const RadioStation &station) const
{
    return "{\"id\":\"" + json_escape(station.id) + "\",\"name\":\"" + json_escape(station.name) +
           "\",\"url\":\"" + json_escape(station.url) + "\",\"country\":\"" +
           json_escape(station.country) + "\"}";
}

std::string RadioBackend::play(const std::string &station_id)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"radio disabled\"}";
    }
    load_bundled();
    load_favorites();

    const RadioStation *station = find_station(station_id);
    if (station == nullptr) {
        return "{\"ok\":false,\"error\":\"station not found\"}";
    }
    if (mpv_ == nullptr) {
        return "{\"ok\":false,\"error\":\"mpv unavailable\"}";
    }
    if (!mpv_->ensure_started()) {
        return "{\"ok\":false,\"error\":\"mpv start failed\"}";
    }

    current_station_id_ = station_id;
    last_metadata_.clear();
    paused_ = false;
    mpv_->mark_playing();
    if (!mpv_->ipc().load_url(station->url)) {
        return "{\"ok\":false,\"error\":\"mpv load failed\"}";
    }

    if (events_ != nullptr) {
        events_->emit("radio.playing", station_event_json(*station));
    }
    return "{\"ok\":true,\"station\":" + station_event_json(*station) + "}";
}

std::string RadioBackend::pause_toggle()
{
    if (mpv_ == nullptr) {
        return "{\"ok\":false,\"error\":\"mpv unavailable\"}";
    }
    mpv_->pause_toggle();
    paused_ = mpv_->is_paused();
    return "{\"ok\":true,\"paused\":" + std::string(paused_ ? "true" : "false") + "}";
}

std::string RadioBackend::stop()
{
    if (mpv_ != nullptr) {
        mpv_->ipc().stop();
    }
    paused_ = false;
    current_station_id_.clear();
    last_metadata_.clear();
    if (events_ != nullptr) {
        events_->emit("radio.ended", "{}");
    }
    return "{\"ok\":true}";
}

std::string RadioBackend::status() const
{
    if (current_station_id_.empty()) {
        return "{\"ok\":true,\"playing\":false}";
    }
    const RadioStation *station = find_station(current_station_id_);
    if (station == nullptr) {
        return "{\"ok\":true,\"playing\":false}";
    }
    return "{\"ok\":true,\"playing\":" + std::string(paused_ ? "false" : "true") +
           ",\"paused\":" + std::string(paused_ ? "true" : "false") +
           ",\"station\":" + station_event_json(*station) +
           ",\"metadata\":\"" + json_escape(last_metadata_) + "\"}";
}

std::string RadioBackend::favorites_add(const std::string &station_id)
{
    load_bundled();
    load_favorites();

    for (const RadioStation &existing : favorites_) {
        if (existing.id == station_id) {
            return "{\"ok\":true,\"already_favorite\":true}";
        }
    }

    const RadioStation *station = find_station(station_id);
    if (station == nullptr) {
        return "{\"ok\":false,\"error\":\"station not found\"}";
    }

    RadioStation fav = *station;
    fav.favorite = true;
    favorites_.push_back(std::move(fav));
    save_favorites();
    return "{\"ok\":true}";
}

std::string RadioBackend::favorites_remove(const std::string &station_id)
{
    load_favorites();
    const size_t before = favorites_.size();
    favorites_.erase(std::remove_if(favorites_.begin(), favorites_.end(),
                                     [&](const RadioStation &s) { return s.id == station_id; }),
                     favorites_.end());
    if (favorites_.size() == before) {
        return "{\"ok\":false,\"error\":\"favorite not found\"}";
    }
    save_favorites();
    return "{\"ok\":true}";
}

std::string RadioBackend::favorites_list()
{
    load_favorites();
    std::ostringstream out;
    out << "{\"ok\":true,\"stations\":[";
    for (size_t i = 0; i < favorites_.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << station_event_json(favorites_[i]);
    }
    out << "]}";
    return out.str();
}

void RadioBackend::poll_metadata()
{
    if (current_station_id_.empty() || paused_) {
        return;
    }
    const RadioStation *station = find_station(current_station_id_);
    if (station == nullptr) {
        return;
    }

    const std::string headers = curl_headers(station->url);
    const std::string title = parse_icy_title(headers);
    if (title.empty() || title == last_metadata_) {
        return;
    }
    last_metadata_ = title;
    if (events_ != nullptr) {
        events_->emit("radio.metadata",
                      "{\"station_id\":\"" + json_escape(station->id) + "\",\"title\":\"" +
                          json_escape(title) + "\"}");
    }
}

} // namespace braillatron::connect
