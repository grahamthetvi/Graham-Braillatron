#include "youtube_backend.h"

#include "json_utils.h"
#include "subprocess.h"

#include <sstream>

namespace braillatron::connect {

YoutubeBackend::YoutubeBackend(YoutubeConfig config, ConnectConfig connect_config, EventWriter *events)
    : config_(std::move(config))
    , connect_config_(std::move(connect_config))
    , events_(events)
    , mpv_(connect_config_.mpv_socket_path)
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

    const std::string cmd = config_.ytdlp_path +
                            " --flat-playlist --dump-json --no-warnings --ignore-errors" +
                            ytdlp_cookie_args() + " \"ytsearch" +
                            std::to_string(config_.search_limit) + ":" + query + "\" 2>/dev/null";

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

bool YoutubeBackend::start_mpv()
{
    if (mpv_proc_.pid > 0) {
        return true;
    }
    const std::string cmd = config_.mpv_path + " --idle=yes --no-video --ao=" + config_.mpv_ao +
                            " --input-ipc-server=" + connect_config_.mpv_socket_path + cookie_args() +
                            " >/dev/null 2>&1";
    mpv_proc_ = spawn_background(cmd);
    return mpv_proc_.pid > 0;
}

void YoutubeBackend::stop_mpv()
{
    mpv_.stop();
    mpv_proc_.stop();
}

std::string YoutubeBackend::play(const std::string &url)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"youtube disabled\"}";
    }
    start_mpv();
    paused_ = false;
    if (!mpv_.load_url(url)) {
        return "{\"ok\":false,\"error\":\"mpv load failed\"}";
    }
    if (events_ != nullptr) {
        events_->emit("youtube.playing", "{\"url\":\"" + json_escape(url) + "\"}");
    }
    return "{\"ok\":true}";
}

std::string YoutubeBackend::pause_toggle()
{
    if (paused_) {
        mpv_.resume();
        paused_ = false;
        if (events_ != nullptr) {
            events_->emit("youtube.playing", "{}");
        }
        return "{\"ok\":true,\"paused\":false}";
    }
    mpv_.pause();
    paused_ = true;
    return "{\"ok\":true,\"paused\":true}";
}

std::string YoutubeBackend::stop()
{
    mpv_.stop();
    paused_ = false;
    if (events_ != nullptr) {
        events_->emit("youtube.ended", "{}");
    }
    return "{\"ok\":true}";
}

} // namespace braillatron::connect
