#include "connect_service.h"

#include "json_utils.h"
#include "subprocess.h"

#include <chrono>
#include <iostream>

namespace braillatron::connect {

namespace {

uint64_t now_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace

ConnectService::ConnectService(ConnectConfig connect_config, YoutubeConfig youtube_config,
                               MessagesConfig messages_config)
    : connect_config_(std::move(connect_config))
    , events_(connect_config_.event_path)
    , youtube_(std::move(youtube_config), connect_config_, &events_)
    , signal_(std::move(messages_config), &events_)
    , server_(connect_config_.socket_path)
{
}

void ConnectService::start()
{
    ensure_directory(connect_config_.credentials_dir);
    ensure_directory(connect_config_.cookies_incoming_dir);
    ensure_directory(connect_config_.credentials_dir + "/signal-cli");

    youtube_.start_mpv();
    signal_.start_daemon_if_linked();
    signal_.start_event_thread();

    if (!server_.listen()) {
        std::cerr << "[connectd] failed to listen on " << connect_config_.socket_path << "\n";
        return;
    }
    running_ = true;
    std::cerr << "[connectd] listening on " << connect_config_.socket_path << "\n";
}

void ConnectService::stop()
{
    running_ = false;
    signal_.stop_event_thread();
    signal_.stop_daemon();
    youtube_.stop_mpv();
    server_.close();
}

void ConnectService::poll()
{
    if (!running_.load()) {
        return;
    }

    const uint64_t now = now_ms();
    if (now - last_cookie_poll_ms_ >= connect_config_.cookie_poll_ms) {
        youtube_.poll_cookie_import();
        last_cookie_poll_ms_ = now;
    }

    server_.poll_once([this](const std::string &request) { return handle_request(request); });
}

std::string ConnectService::cmd_from_request(const std::string &request) const
{
    return json_get_string(request, "cmd");
}

std::string ConnectService::handle_request(const std::string &request)
{
    const std::string cmd = cmd_from_request(request);
    if (cmd == "ping") {
        return "{\"ok\":true,\"service\":\"connectd\"}";
    }
    if (cmd == "accounts.status") {
        const bool youtube_cookies = youtube_.cookies_present();
        return "{\"ok\":true,\"youtube_cookies\":" +
               std::string(youtube_cookies ? "true" : "false") + ",\"signal_linked\":" +
               std::string(signal_.is_linked() ? "true" : "false") + "}";
    }
    if (cmd == "youtube.search") {
        return youtube_.search(json_get_string(request, "query"));
    }
    if (cmd == "youtube.play") {
        return youtube_.play(json_get_string(request, "url"));
    }
    if (cmd == "youtube.pause") {
        return youtube_.pause_toggle();
    }
    if (cmd == "youtube.stop") {
        return youtube_.stop();
    }
    if (cmd == "youtube.import_cookies") {
        youtube_.poll_cookie_import();
        return "{\"ok\":true,\"cookies\":" +
               std::string(youtube_.cookies_present() ? "true" : "false") + "}";
    }
    if (cmd == "signal.start_link") {
        return signal_.start_link();
    }
    if (cmd == "signal.finish_link") {
        return signal_.finish_link();
    }
    if (cmd == "signal.list_chats") {
        return signal_.list_chats();
    }
    if (cmd == "signal.list_messages") {
        return signal_.list_messages(json_get_string(request, "recipient"));
    }
    if (cmd == "signal.send") {
        return signal_.send_message(json_get_string(request, "recipient"),
                                    json_get_string(request, "text"));
    }
    return "{\"ok\":false,\"error\":\"unknown cmd\"}";
}

} // namespace braillatron::connect
