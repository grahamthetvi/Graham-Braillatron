#include "remote_display_config.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <filesystem>

namespace braillatron::display {

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

} // namespace

std::string resolve_config_path(const std::string &base, const std::string &path)
{
    if (path.empty() || path[0] == '/') {
        return path;
    }
    return base + "/" + path;
}

std::string remote_display_config_path_from_env()
{
    const char *env = std::getenv("BRAILLATRON_REMOTE_DISPLAY_CONFIG");
    if (env != nullptr && env[0] != '\0') {
        return env;
    }
    if (std::filesystem::exists("/data/braillatron/settings/remote-display.conf")) {
        return "/data/braillatron/settings/remote-display.conf";
    }
    return "/etc/braillatron/remote-display.conf";
}

RemoteDisplayConfig load_remote_display_config(const std::string &path)
{
    RemoteDisplayConfig config;
    std::ifstream file(path);
    if (!file.is_open()) {
        return config;
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
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));

        if (key == "enabled") {
            config.enabled = parse_bool(value);
        } else if (key == "listen_address") {
            config.listen_address = value;
        } else if (key == "listen_port") {
            config.listen_port = static_cast<uint16_t>(std::stoul(value));
        } else if (key == "allow_lan") {
            config.allow_lan = parse_bool(value);
        } else if (key == "session_idle_minutes") {
            config.session_idle_minutes = static_cast<uint32_t>(std::stoul(value));
        } else if (key == "pairing_code_hash") {
            config.pairing_code_hash = value;
        } else if (key == "frame_socket_path") {
            config.frame_socket_path = value;
        } else if (key == "cmd_socket_path") {
            config.cmd_socket_path = value;
        } else if (key == "static_root") {
            config.static_root = value;
        }
    }
    return config;
}

bool save_remote_display_config(const std::string &path, const RemoteDisplayConfig &config)
{
    const std::string tmp = path + ".tmp";
    std::ofstream out(tmp);
    if (!out.is_open()) {
        return false;
    }

    out << "enabled=" << (config.enabled ? "true" : "false") << "\n";
    out << "listen_address=" << config.listen_address << "\n";
    out << "listen_port=" << config.listen_port << "\n";
    out << "allow_lan=" << (config.allow_lan ? "true" : "false") << "\n";
    out << "session_idle_minutes=" << config.session_idle_minutes << "\n";
    out << "pairing_code_hash=" << config.pairing_code_hash << "\n";
    out << "frame_socket_path=" << config.frame_socket_path << "\n";
    out << "cmd_socket_path=" << config.cmd_socket_path << "\n";
    out << "static_root=" << config.static_root << "\n";
    out.flush();
    out.close();
    std::filesystem::rename(tmp, path);
    return true;
}

} // namespace braillatron::display
