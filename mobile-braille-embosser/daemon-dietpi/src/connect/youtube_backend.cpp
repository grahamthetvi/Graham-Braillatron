#include "youtube_backend.h"

#include "json_utils.h"
#include "subprocess.h"

#include <sstream>

namespace braillatron::connect {

namespace {

std::string trim(const std::string &value)
{
    const size_t start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return {};
    }
    const size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string shell_escape(const std::string &value)
{
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out += ch;
        }
    }
    out += "'";
    return out;
}

YoutubeResult parse_video_line(const std::string &line)
{
    YoutubeResult item;
    item.id = json_get_string(line, "id");
    item.title = json_get_string(line, "title");
    item.url = json_get_string(line, "url");
    item.channel = json_get_string(line, "channel");
    if (item.channel.empty()) {
        item.channel = json_get_string(line, "uploader");
    }
    item.duration = json_get_string(line, "duration_string");
    if (item.title.empty() || item.id.empty()) {
        item.title.clear();
        return item;
    }
    if (item.url.empty()) {
        if (item.id.find('/') != std::string::npos) {
            item.url = "https://www.youtube.com" + item.id;
        } else {
            item.url = "https://www.youtube.com/watch?v=" + item.id;
        }
    }
    return item;
}

std::string normalize_youtube_url(const std::string &url)
{
    if (url.empty()) {
        return {};
    }

    if (url.find('/') == std::string::npos && url.find('?') == std::string::npos &&
        url.find('&') == std::string::npos && url.size() >= 8 && url.size() <= 15) {
        return "https://www.youtube.com/watch?v=" + url;
    }

    const std::string marker = "v=";
    const size_t pos = url.find(marker);
    if (pos != std::string::npos) {
        const size_t start = pos + marker.size();
        const size_t end = url.find_first_of("&?#", start);
        const std::string id = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!id.empty()) {
            return "https://www.youtube.com/watch?v=" + id;
        }
    }

    const std::string short_marker = "youtu.be/";
    const size_t short_pos = url.find(short_marker);
    if (short_pos != std::string::npos) {
        const size_t start = short_pos + short_marker.size();
        const size_t end = url.find_first_of("?&#", start);
        const std::string id = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!id.empty()) {
            return "https://www.youtube.com/watch?v=" + id;
        }
    }

    return url;
}

} // namespace

YoutubeBackend::YoutubeBackend(YoutubeConfig config, ConnectConfig connect_config, MpvService *mpv,
                               EventWriter *events)
    : config_(std::move(config))
    , connect_config_(std::move(connect_config))
    , mpv_(mpv)
    , events_(events)
{
}

bool YoutubeBackend::cookies_present() const
{
    return file_exists(config_.cookies_path);
}

std::string YoutubeBackend::mpv_extra_args(const YoutubeConfig &config)
{
    std::string args;
    if (!config.ytdlp_path.empty()) {
        args += " --script-opts=ytdl_hook-ytdl_path=" + config.ytdlp_path;
    }
    if (file_exists(config.cookies_path)) {
        args += " --ytdl-raw-options=cookies=" + config.cookies_path;
    }
    return args;
}

std::string YoutubeBackend::cookie_args() const
{
    if (!cookies_present()) {
        return {};
    }
    return " --ytdl-raw-options=cookies=" + config_.cookies_path;
}

std::string YoutubeBackend::ytdlp_cookie_args() const
{
    if (!cookies_present()) {
        return {};
    }
    return " --cookies \"" + config_.cookies_path + "\"";
}

void YoutubeBackend::poll_cookie_import()
{
    const std::string incoming = connect_config_.cookies_incoming_dir + "/cookies.txt";
    if (!file_exists(incoming)) {
        return;
    }
    if (atomic_move_file(incoming, config_.cookies_path)) {
        if (events_ != nullptr) {
            events_->emit("auth.cleared", "{\"service\":\"youtube\"}");
        }
    }
}

std::vector<YoutubeResult> YoutubeBackend::fetch_playlist(const std::string &source,
                                                          uint32_t limit) const
{
    if (source.empty()) {
        return {};
    }

    const std::string cmd = config_.ytdlp_path +
                            " --flat-playlist --dump-json --no-warnings --ignore-errors" +
                            " --playlist-end " + std::to_string(limit) + ytdlp_cookie_args() +
                            " " + shell_escape(source) + " 2>/dev/null";

    const std::string output = run_command(cmd);
    std::vector<YoutubeResult> results;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        YoutubeResult item = parse_video_line(line);
        if (item.title.empty()) {
            continue;
        }
        results.push_back(std::move(item));
    }
    return results;
}

std::string YoutubeBackend::resolve_stream_url(const std::string &url) const
{
    const std::string normalized = normalize_youtube_url(url);
    if (normalized.empty()) {
        return {};
    }

    const std::string cmd = config_.ytdlp_path +
                            " -f bestaudio/best --get-url --no-playlist --no-warnings" +
                            ytdlp_cookie_args() + " " + shell_escape(normalized) + " 2>/dev/null";
    const std::string output = trim(run_command(cmd));
    if (output.empty() || output.rfind("http", 0) != 0) {
        return {};
    }
    return output;
}

std::string YoutubeBackend::format_results(const std::vector<YoutubeResult> &results,
                                           const std::string &feed, bool personalized) const
{
    if (results.empty()) {
        return "{\"ok\":false,\"error\":\"no results\",\"feed\":\"" + json_escape(feed) +
               "\",\"personalized\":" + std::string(personalized ? "true" : "false") + "}";
    }

    std::ostringstream out;
    out << "{\"ok\":true,\"feed\":\"" << json_escape(feed) << "\",\"personalized\":"
        << (personalized ? "true" : "false") << ",\"results\":[";
    for (size_t i = 0; i < results.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        const YoutubeResult &item = results[i];
        out << "{\"id\":\"" << json_escape(item.id) << "\",\"title\":\"" << json_escape(item.title)
            << "\",\"url\":\"" << json_escape(item.url) << "\",\"channel\":\""
            << json_escape(item.channel) << "\",\"duration\":\"" << json_escape(item.duration)
            << "\"}";
    }
    out << "]}";
    return out.str();
}

std::string YoutubeBackend::search(const std::string &query)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"youtube disabled\"}";
    }
    if (query.empty()) {
        return "{\"ok\":false,\"error\":\"empty query\"}";
    }

    const std::string search_term =
        "ytsearch" + std::to_string(config_.search_limit) + ":" + query;
    const std::vector<YoutubeResult> results =
        fetch_playlist(search_term, config_.search_limit);
    return format_results(results, "search", false);
}

std::string YoutubeBackend::recommended()
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"youtube disabled\"}";
    }

    bool personalized = cookies_present();
    std::vector<YoutubeResult> results;
    if (personalized) {
        results = fetch_playlist(config_.recommended_url, config_.feed_limit);
    }
    if (results.empty()) {
        personalized = false;
        results = fetch_playlist(config_.recommended_fallback_url, config_.feed_limit);
    }
    if (results.empty() && config_.recommended_fallback_url != "ytsearch15:popular") {
        results = fetch_playlist("ytsearch" + std::to_string(config_.feed_limit) + ":popular",
                                 config_.feed_limit);
    }
    return format_results(results, "recommended", personalized);
}

std::string YoutubeBackend::shorts()
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"youtube disabled\"}";
    }

    const std::vector<YoutubeResult> results =
        fetch_playlist(config_.shorts_url, config_.feed_limit);
    return format_results(results, "shorts", false);
}

std::string YoutubeBackend::play(const std::string &url)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"youtube disabled\"}";
    }
    if (url.empty()) {
        return "{\"ok\":false,\"error\":\"empty url\"}";
    }
    const std::string normalized = normalize_youtube_url(url);
    if (normalized.empty()) {
        return "{\"ok\":false,\"error\":\"invalid url\"}";
    }
    if (mpv_ == nullptr || !mpv_->ensure_started()) {
        return "{\"ok\":false,\"error\":\"mpv start failed\"}";
    }
    paused_ = false;
    mpv_->mark_playing();
    bool loaded = mpv_->ipc().load_url(normalized);
    if (!loaded) {
        loaded = mpv_->ensure_started() && mpv_->ipc().load_url(normalized);
    }
    if (!loaded) {
        return "{\"ok\":false,\"error\":\"mpv load failed\"}";
    }
    if (events_ != nullptr) {
        events_->emit("youtube.playing",
                       "{\"url\":\"" + json_escape(normalized) + "\"}");
    }
    return "{\"ok\":true}";
}

std::string YoutubeBackend::pause_toggle()
{
    if (mpv_ == nullptr) {
        return "{\"ok\":false,\"error\":\"mpv unavailable\"}";
    }
    mpv_->pause_toggle();
    paused_ = mpv_->is_paused();
    if (!paused_ && events_ != nullptr) {
        events_->emit("youtube.playing", "{}");
    }
    return "{\"ok\":true,\"paused\":" + std::string(paused_ ? "true" : "false") + "}";
}

std::string YoutubeBackend::stop()
{
    if (mpv_ != nullptr) {
        mpv_->ipc().stop();
    }
    paused_ = false;
    if (events_ != nullptr) {
        events_->emit("youtube.ended", "{}");
    }
    return "{\"ok\":true}";
}

} // namespace braillatron::connect
