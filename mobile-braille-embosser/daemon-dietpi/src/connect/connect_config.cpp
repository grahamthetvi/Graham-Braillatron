#include "connect_config.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>

namespace braillatron::connect {

namespace {

std::string trim(const std::string &value)
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

bool parse_bool(const std::string &value)
{
    const std::string lower = trim(value);
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
}

template <typename Config, typename ApplyFn>
void load_key_value_file(const std::string &path, ApplyFn apply, Config &config)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        apply(trim(line.substr(0, eq)), trim(line.substr(eq + 1)), config);
    }
}

} // namespace

std::string config_dir_from_env()
{
    const char *env = std::getenv("BRAILLATRON_CONFIG");
    if (env != nullptr && env[0] != '\0') {
        return env;
    }
    return "config";
}

std::string resolve_config_path(const std::string &base, const std::string &path)
{
    if (path.empty() || path[0] == '/') {
        return path;
    }
    return base + "/" + path;
}

ConnectConfig load_connect_config(const std::string &path)
{
    ConnectConfig config;
    load_key_value_file(path,
                        [](const std::string &key, const std::string &value, ConnectConfig &cfg) {
                            if (key == "socket_path") {
                                cfg.socket_path = value;
                            } else if (key == "event_path") {
                                cfg.event_path = value;
                            } else if (key == "mpv_socket_path") {
                                cfg.mpv_socket_path = value;
                            } else if (key == "credentials_dir") {
                                cfg.credentials_dir = value;
                            } else if (key == "cookies_incoming_dir") {
                                cfg.cookies_incoming_dir = value;
                            } else if (key == "cookie_poll_ms") {
                                cfg.cookie_poll_ms = static_cast<uint32_t>(std::stoul(value));
                            }
                        },
                        config);
    return config;
}

YoutubeConfig load_youtube_config(const std::string &path)
{
    YoutubeConfig config;
    load_key_value_file(path,
                        [](const std::string &key, const std::string &value, YoutubeConfig &cfg) {
                            if (key == "enabled") {
                                cfg.enabled = parse_bool(value);
                            } else if (key == "cookies_path") {
                                cfg.cookies_path = value;
                            } else if (key == "mpv_ao") {
                                cfg.mpv_ao = value;
                            } else if (key == "search_limit") {
                                cfg.search_limit = static_cast<uint32_t>(std::stoul(value));
                            } else if (key == "ytdlp_path") {
                                cfg.ytdlp_path = value;
                            } else if (key == "mpv_path") {
                                cfg.mpv_path = value;
                            }
                        },
                        config);
    return config;
}

MessagesConfig load_messages_config(const std::string &path)
{
    MessagesConfig config;
    load_key_value_file(path,
                        [](const std::string &key, const std::string &value, MessagesConfig &cfg) {
                            if (key == "enabled") {
                                cfg.enabled = parse_bool(value);
                            } else if (key == "signal_cli_path") {
                                cfg.signal_cli_path = value;
                            } else if (key == "signal_data_dir") {
                                cfg.signal_data_dir = value;
                            } else if (key == "signal_http") {
                                cfg.signal_http = value;
                            } else if (key == "device_name") {
                                cfg.device_name = value;
                            } else if (key == "link_timeout_sec") {
                                cfg.link_timeout_sec = static_cast<uint32_t>(std::stoul(value));
                            }
                        },
                        config);
    return config;
}

} // namespace braillatron::connect
