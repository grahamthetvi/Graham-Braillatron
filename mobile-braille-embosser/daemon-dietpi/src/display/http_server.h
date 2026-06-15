#pragma once

#include "pairing_auth.h"
#include "remote_display_config.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace braillatron::display {

class HttpServer {
public:
    HttpServer(RemoteDisplayConfig config, PairingAuth &auth);
    ~HttpServer();

    HttpServer(const HttpServer &) = delete;
    HttpServer &operator=(const HttpServer &) = delete;

    bool start();
    void stop();
    void publish_frame(const std::vector<uint16_t> &pixels, uint16_t width, uint16_t height);
    size_t client_count() const;

private:
    void accept_loop();
    void handle_client(int client_fd);
    bool handle_http_request(int client_fd, const std::string &request);
    bool handle_websocket(int client_fd, const std::string &request);
    void send_websocket_binary(int client_fd, const uint8_t *data, size_t len);
    std::string resolve_static(const std::string &path) const;
    std::string session_from_cookie(const std::string &cookie_header) const;

    RemoteDisplayConfig config_;
    PairingAuth &auth_;
    int listen_fd_ = -1;
    std::atomic<bool> running_ {false};
    mutable std::mutex clients_mutex_;
    std::vector<int> websocket_clients_;
    std::vector<uint16_t> latest_pixels_;
    uint16_t latest_width_ = 0;
    uint16_t latest_height_ = 0;
    uint32_t latest_frame_id_ = 0;
};

} // namespace braillatron::display
