#pragma once

#include <cstdint>
#include <string>

namespace braillatron::display {

struct RemoteDisplayConfig {
    bool enabled = false;
    std::string listen_address = "127.0.0.1";
    uint16_t listen_port = 8080;
    bool allow_lan = false;
    uint32_t session_idle_minutes = 30;
    std::string pairing_code_hash;
    std::string frame_socket_path = "/run/braillatron/display.sock";
    std::string cmd_socket_path = "/run/braillatron/display-cmd.sock";
    std::string static_root = "/usr/share/braillatron/remote-display";
};

RemoteDisplayConfig load_remote_display_config(const std::string &path);
bool save_remote_display_config(const std::string &path, const RemoteDisplayConfig &config);

std::string remote_display_config_path_from_env();
std::string resolve_config_path(const std::string &base, const std::string &path);

} // namespace braillatron::display
