#include "youtube_backend.h"

#include "json_utils.h"
#include "subprocess.h"

#include <sstream>

namespace braillatron::connect {

namespace {

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

std::string YoutubeBackend::search(const std::string &query)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"youtube disabled\"}";
    }

    const std::string search_term =
        "ytsearch" + std::to_string(config_.search_limit) + ":" + query;
    const std::string cmd = config_.ytdlp_path +
                            " --flat-playlist --dump-json --no-warnings --ignore-errors" +
                            ytdlp_cookie_args() + " " + shell_escape(search_term) + " 2>/dev/null";

    const std::string output = run_command(cmd);
    std::ostringstream results;
    results << "{\"ok\":true,\"results\":[";
    bool first = true;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        const std::string id = json_get_string(line, "id");
        const std::string title = json_get_string(line, "title");
        const std::string url = json_get_string(line, "url");
        if (title.empty()) {
            continue;
        }
        if (!first) {
            results << ',';
        }
        first = false;
        results << "{\"id\":\"" << json_escape(id) << "\",\"title\":\"" << json_escape(title)
                << "\",\"url\":\"" << json_escape(url.empty() ? ("https://www.youtube.com/watch?v=" + id)
                                                             : url)
                << "\"}";
    }
    results << "]}";
    return results.str();
}

std::string YoutubeBackend::play(const std::string &url)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"youtube disabled\"}";
    }
    if (mpv_ == nullptr || !mpv_->ensure_started()) {
        return "{\"ok\":false,\"error\":\"mpv start failed\"}";
    }
    paused_ = false;
    mpv_->mark_playing();
    bool loaded = mpv_->ipc().load_url(url);
    if (!loaded) {
        loaded = mpv_->ensure_started() && mpv_->ipc().load_url(url);
    }
    if (!loaded) {
        return "{\"ok\":false,\"error\":\"mpv load failed\"}";
    }
    if (events_ != nullptr) {
        events_->emit("youtube.playing", "{\"url\":\"" + json_escape(url) + "\"}");
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
