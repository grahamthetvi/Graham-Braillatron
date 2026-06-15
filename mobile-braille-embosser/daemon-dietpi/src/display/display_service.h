#pragma once

#include "http_server.h"
#include "pairing_auth.h"
#include "remote_display_config.h"

#include <memory>

namespace braillatron::display {

class DisplayService {
public:
    explicit DisplayService(RemoteDisplayConfig config);

    void start();
    void stop();
    void poll();
    void reload_config(const RemoteDisplayConfig &config);
    PairingAuth &auth() { return auth_; }

private:
    bool ensure_frame_listener();
    bool ensure_cmd_listener();
    void close_frame_listener();
    void close_cmd_listener();
    void accept_frame_client();
    void accept_cmd_client();
    void read_frame_from_client(int client_fd);
    void handle_cmd_line(const std::string &line);

    RemoteDisplayConfig config_;
    PairingAuth auth_;
    std::unique_ptr<HttpServer> http_server_;
    int frame_listen_fd_ = -1;
    int cmd_listen_fd_ = -1;
    int active_frame_client_ = -1;
};

} // namespace braillatron::display
