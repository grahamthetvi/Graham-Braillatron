#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

struct MusicConfig {
    bool enabled = true;
    std::string music_dir = "/data/braillatron/music";
    std::string state_path = "/data/braillatron/music/state.json";
    std::string mpv_path = "mpv";
    std::string mpv_ao = "pulse";
    std::vector<std::string> extensions = {".mp3",  ".flac", ".ogg",  ".oga", ".opus",
                                           ".m4a",  ".aac",  ".wav",  ".wma"};
};

struct WeatherConfig {
    bool enabled = true;
    double latitude = 0.0;
    double longitude = 0.0;
    std::string city_name;
    std::string provider_url = "https://api.open-meteo.com/v1/forecast";
    std::string geocoding_url = "https://geocoding-api.open-meteo.com/v1/search";
    std::string cache_path = "/data/braillatron/weather/cache.json";
    uint32_t cache_ttl_sec = 1800;
    std::string temperature_unit = "celsius";
    uint32_t hourly_limit = 24;
    uint32_t daily_limit = 7;
};

struct PodcastsConfig {
    bool enabled = true;
    std::string feeds_path = "/data/braillatron/podcasts/feeds.json";
    std::string download_dir = "/data/braillatron/podcasts/downloads";
    std::string import_dir = "/data/braillatron/podcasts/import";
    uint32_t refresh_interval_sec = 3600;
    uint32_t max_episodes_per_feed = 50;
    std::string user_agent = "Braillatron/1.0 (accessibility device)";
};

struct RadioConfig {
    bool enabled = true;
    std::string stations_path = "/usr/share/braillatron/radio/stations.json";
    std::string favorites_path = "/data/braillatron/radio/favorites.json";
    std::string radio_browser_url = "https://de1.api.radio-browser.info";
    std::string default_country = "US";
    uint32_t search_limit = 20;
    uint32_t metadata_poll_sec = 30;
};

struct LibraryConfig {
    bool enabled = true;
    std::string gutendex_url = "https://gutendex.com/books";
    std::string download_dir = "/data/braillatron/library/books";
    std::string catalog_path = "/data/braillatron/library/catalog.json";
    uint32_t search_limit = 10;
    std::string user_agent = "Braillatron/1.0 (accessibility device)";
    std::string preferred_format = "epub";
};

struct GmailConfig {
    bool enabled = true;
    std::string credentials_dir = "/data/braillatron/credentials/gmail";
    std::string client_id_path = "/data/braillatron/credentials/gmail/client_id";
    std::string token_path = "/data/braillatron/credentials/gmail/token.json";
    std::string scopes =
        "https://www.googleapis.com/auth/gmail.readonly "
        "https://www.googleapis.com/auth/gmail.send "
        "https://www.googleapis.com/auth/gmail.modify";
    uint32_t link_timeout_sec = 300;
    uint32_t inbox_limit = 25;
    std::string export_dir = "/data/braillatron/documents/gmail";
    std::string user_agent = "Braillatron/1.0 (accessibility device)";
};

ConnectConfig load_connect_config(const std::string &path);
YoutubeConfig load_youtube_config(const std::string &path);
MessagesConfig load_messages_config(const std::string &path);
MusicConfig load_music_config(const std::string &path);
WeatherConfig load_weather_config(const std::string &path);
PodcastsConfig load_podcasts_config(const std::string &path);
RadioConfig load_radio_config(const std::string &path);
LibraryConfig load_library_config(const std::string &path);
GmailConfig load_gmail_config(const std::string &path);

std::string config_dir_from_env();
std::string resolve_config_path(const std::string &base, const std::string &path);

} // namespace braillatron::connect
