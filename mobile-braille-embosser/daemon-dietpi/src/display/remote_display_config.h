#pragma once

#include <cstdint>
#include <string>

namespace braillatron::display {

struct RemoteDisplayConfig {
    bool enabled = false;
    std::string frame_socket = "/run/braillatron/display.sock";
    std::string cmd_socket = "/run/braillatron/display-cmd.sock";
    std::string listen_address = "127.0.0.1";
    uint16_t listen_port = 8080;
    bool allow_lan = false;
    uint32_t session_idle_minutes = 30;
    std::string pairing_code_hash;
    std::string static_dir = "/usr/share/braillatron/remote-display";
};

std::string remote_display_config_path();
RemoteDisplayConfig load_remote_display_config(const std::string &path);
void save_remote_display_config(const std::string &path, const RemoteDisplayConfig &config);

} // namespace braillatron::display
