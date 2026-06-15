#include "remote_display_config.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <unistd.h>

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

std::string remote_display_config_path()
{
    const char *env = std::getenv("BRAILLATRON_REMOTE_DISPLAY_CONFIG");
    if (env != nullptr && env[0] != '\0') {
        return env;
    }
    if (access("/data/braillatron/settings/remote-display.conf", R_OK | W_OK) == 0) {
        return "/data/braillatron/settings/remote-display.conf";
    }
    const char *config_env = std::getenv("BRAILLATRON_CONFIG");
    if (config_env != nullptr && config_env[0] != '\0') {
        return std::string(config_env) + "/remote-display.conf";
    }
    return "config/remote-display.conf";
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
        } else if (key == "frame_socket") {
            config.frame_socket = value;
        } else if (key == "cmd_socket") {
            config.cmd_socket = value;
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
        } else if (key == "static_dir") {
            config.static_dir = value;
        }
    }

    return config;
}

void save_remote_display_config(const std::string &path, const RemoteDisplayConfig &config)
{
    const std::string tmp = path + ".tmp";
    std::ofstream file(tmp);
    if (!file.is_open()) {
        return;
    }
    file << "# Braillatron remote display settings\n";
    file << "enabled=" << (config.enabled ? "true" : "false") << "\n";
    file << "frame_socket=" << config.frame_socket << "\n";
    file << "cmd_socket=" << config.cmd_socket << "\n";
    file << "listen_address=" << config.listen_address << "\n";
    file << "listen_port=" << config.listen_port << "\n";
    file << "allow_lan=" << (config.allow_lan ? "true" : "false") << "\n";
    file << "session_idle_minutes=" << config.session_idle_minutes << "\n";
    file << "pairing_code_hash=" << config.pairing_code_hash << "\n";
    file << "static_dir=" << config.static_dir << "\n";
    file.flush();
    std::rename(tmp.c_str(), path.c_str());
}

} // namespace braillatron::display
