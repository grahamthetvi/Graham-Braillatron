#pragma once

#include "frame_protocol.h"
#include "pairing_auth.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace braillatron::display {

class VirtualKeyboard;

class HttpServer {
public:
    HttpServer(std::string bind_address, uint16_t port, std::string static_root, PairingAuth *auth, VirtualKeyboard *kb = nullptr);

    bool start();
    void stop();
    ~HttpServer();
    void broadcast_frame(const FrameHeader &header, const std::vector<uint16_t> &pixels);
    void broadcast_speak(const std::string &text);

    uint32_t connected_clients() const { return connected_clients_; }
    bool running() const { return running_; }

private:
    struct Client {
        int fd = -1;
        bool websocket = false;
    };

    void accept_loop();
    void handle_client(int fd);
    bool handle_http_request(int fd, const std::string &request);
    bool upgrade_websocket(int fd, const std::string &request, const std::string &session_token);
    void send_http_response(int fd, int status, const std::string &content_type,
                            const std::string &body, const std::string &extra_headers = "");
    std::string read_static_file(const std::string &path) const;
    std::string session_from_cookie(const std::string &request) const;
    void remove_client(int fd);

    std::string bind_address_;
    uint16_t port_;
    std::string static_root_;
    PairingAuth *auth_ = nullptr;
    VirtualKeyboard *kb_ = nullptr;
    int listen_fd_ = -1;
    std::atomic<bool> running_ {false};
    std::thread accept_thread_;
    std::mutex clients_mutex_;
    std::vector<Client> clients_;
    std::atomic<uint32_t> connected_clients_ {0};

    bool has_frame_ = false;
    FrameHeader latest_header_ {};
    std::vector<uint16_t> latest_pixels_;

public:
    void poll_clients();
private:
    void handle_websocket_message(int fd, const std::string &msg);
};

} // namespace braillatron::display
