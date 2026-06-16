#pragma once

#include "frame_subscriber.h"
#include "http_server.h"
#include "pairing_auth.h"
#include "remote_display_config.h"
#include "virtual_keyboard.h"

#include "../connect/socket_server.h"

#include <memory>
#include <string>

namespace braillatron::display {

class DisplayService {
public:
    explicit DisplayService(RemoteDisplayConfig config);

    bool start();
    void stop();
    void poll();

    RemoteDisplayConfig &config() { return config_; }
    PairingAuth &auth() { return auth_; }

private:
    std::string handle_command(const std::string &request);
    void apply_network_bind();

    RemoteDisplayConfig config_;
    PairingAuth auth_;
    VirtualKeyboard virtual_keyboard_;
    FrameSubscriber frame_subscriber_;
    std::unique_ptr<HttpServer> http_server_;
    connect::SocketServer cmd_server_;
    bool running_ = false;
};

} // namespace braillatron::display
