#pragma once

#include <cstdint>
#include <string>

namespace braillatron::connect {

struct ConnectConfig {
    std::string socket_path = "/run/braillatron/connect.sock";
    std::string event_path = "/run/braillatron/connect.events";
    std::string mpv_socket_path = "/run/braillatron/mpv.sock";
    std::string credentials_dir = "/data/braillatron/credentials";
    std::string cookies_incoming_dir = "/data/braillatron/credentials/incoming";
    uint32_t cookie_poll_ms = 30000;
};

struct YoutubeConfig {
    bool enabled = true;
    std::string cookies_path = "/data/braillatron/credentials/youtube-cookies.txt";
    std::string mpv_ao = "pulse";
    uint32_t search_limit = 10;
    std::string ytdlp_path = "yt-dlp";
    std::string mpv_path = "mpv";
};

struct MessagesConfig {
    bool enabled = true;
    std::string signal_cli_path = "/usr/local/bin/signal-cli";
    std::string signal_data_dir = "/data/braillatron/credentials/signal-cli";
    std::string signal_http = "127.0.0.1:18080";
    std::string device_name = "Braillatron";
    uint32_t link_timeout_sec = 120;
};

ConnectConfig load_connect_config(const std::string &path);
YoutubeConfig load_youtube_config(const std::string &path);
MessagesConfig load_messages_config(const std::string &path);

std::string config_dir_from_env();
std::string resolve_config_path(const std::string &base, const std::string &path);

} // namespace braillatron::connect
