#include "display_service.h"

#include <memory>

#include "frame_protocol.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace braillatron::display {

namespace {

constexpr auto kPairingLifetime = std::chrono::minutes(5);

} // namespace

DisplayService::DisplayService(RemoteDisplayConfig config)
    : config_(std::move(config))
    , auth_(config_.session_idle_minutes)
{
}

void DisplayService::start()
{
    ensure_frame_listener();
    ensure_cmd_listener();
    if (config_.enabled) {
        if (!http_server_) {
            http_server_ = std::make_unique<HttpServer>(config_, auth_);
        }
        http_server_->start();
    }
}

void DisplayService::stop()
{
    if (http_server_) {
        http_server_->stop();
        http_server_.reset();
    }
    if (active_frame_client_ >= 0) {
        close(active_frame_client_);
        active_frame_client_ = -1;
    }
    close_frame_listener();
    close_cmd_listener();
}

void DisplayService::reload_config(const RemoteDisplayConfig &config)
{
    const bool was_enabled = config_.enabled;
    config_ = config;
    if (config_.enabled) {
        if (!http_server_) {
            http_server_ = std::make_unique<HttpServer>(config_, auth_);
        } else {
            http_server_->stop();
            http_server_ = std::make_unique<HttpServer>(config_, auth_);
        }
        http_server_->start();
    } else if (was_enabled && http_server_) {
        http_server_->stop();
        http_server_.reset();
    }
}

void DisplayService::poll()
{
    auth_.prune_expired_sessions();
    ensure_frame_listener();
    ensure_cmd_listener();
    accept_frame_client();
    accept_cmd_client();
    if (active_frame_client_ >= 0) {
        read_frame_from_client(active_frame_client_);
    }
}

bool DisplayService::ensure_frame_listener()
{
    if (frame_listen_fd_ >= 0) {
        return true;
    }
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (config_.frame_socket.size() >= sizeof(addr.sun_path)) {
        close(fd);
        return false;
    }
    std::strncpy(addr.sun_path, config_.frame_socket.c_str(), sizeof(addr.sun_path) - 1);
    unlink(config_.frame_socket.c_str());
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return false;
    }
    if (listen(fd, 1) < 0) {
        close(fd);
        return false;
    }
    frame_listen_fd_ = fd;
    return true;
}

bool DisplayService::ensure_cmd_listener()
{
    if (cmd_listen_fd_ >= 0) {
        return true;
    }
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (config_.cmd_socket.size() >= sizeof(addr.sun_path)) {
        close(fd);
        return false;
    }
    std::strncpy(addr.sun_path, config_.cmd_socket.c_str(), sizeof(addr.sun_path) - 1);
    unlink(config_.cmd_socket.c_str());
    if (bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return false;
    }
    if (listen(fd, 2) < 0) {
        close(fd);
        return false;
    }
    cmd_listen_fd_ = fd;
    return true;
}

void DisplayService::close_frame_listener()
{
    if (frame_listen_fd_ >= 0) {
        close(frame_listen_fd_);
        frame_listen_fd_ = -1;
    }
    unlink(config_.frame_socket.c_str());
}

void DisplayService::close_cmd_listener()
{
    if (cmd_listen_fd_ >= 0) {
        close(cmd_listen_fd_);
        cmd_listen_fd_ = -1;
    }
    unlink(config_.cmd_socket.c_str());
}

void DisplayService::accept_frame_client()
{
    if (frame_listen_fd_ < 0 || active_frame_client_ >= 0) {
        return;
    }
    const int client = accept(frame_listen_fd_, nullptr, nullptr);
    if (client >= 0) {
        active_frame_client_ = client;
    }
}

void DisplayService::accept_cmd_client()
{
    if (cmd_listen_fd_ < 0) {
        return;
    }
    const int client = accept(cmd_listen_fd_, nullptr, nullptr);
    if (client < 0) {
        return;
    }
    std::string buffer;
    char chunk[512];
    const ssize_t n = recv(client, chunk, sizeof(chunk) - 1, 0);
    if (n > 0) {
        chunk[n] = '\0';
        buffer.assign(chunk, static_cast<size_t>(n));
        handle_cmd_line(buffer);
    }
    close(client);
}

void DisplayService::read_frame_from_client(int client_fd)
{
    FrameHeader header {};
    ssize_t n = recv(client_fd, &header, sizeof(header), MSG_DONTWAIT);
    if (n == 0) {
        close(client_fd);
        active_frame_client_ = -1;
        return;
    }
    if (n < 0) {
        return;
    }
    if (static_cast<size_t>(n) < sizeof(header)) {
        return;
    }
    if (!validate_frame_header(header)) {
        return;
    }

    std::vector<uint16_t> pixels(header.payload_bytes / sizeof(uint16_t));
    size_t offset = 0;
    while (offset < pixels.size() * sizeof(uint16_t)) {
        n = recv(client_fd, reinterpret_cast<uint8_t *>(pixels.data()) + offset,
                 pixels.size() * sizeof(uint16_t) - offset, MSG_DONTWAIT);
        if (n <= 0) {
            return;
        }
        offset += static_cast<size_t>(n);
    }

    if (http_server_) {
        http_server_->publish_frame(pixels, header.width, header.height);
    }
}

void DisplayService::handle_cmd_line(const std::string &line)
{
    if (line.find("\"cmd\":\"pairing.start\"") == std::string::npos &&
        line.find("pairing.start") == std::string::npos) {
        return;
    }

    std::string code;
    const size_t code_key = line.find("\"code\"");
    if (code_key != std::string::npos) {
        const size_t quote = line.find('"', code_key + 6);
        const size_t quote2 = line.find('"', quote + 1);
        if (quote != std::string::npos && quote2 != std::string::npos) {
            code = line.substr(quote + 1, quote2 - quote - 1);
        }
    }
    if (code.empty()) {
        return;
    }

    const auto expires = std::chrono::steady_clock::now() + kPairingLifetime;
    auth_.set_active_pairing_hash(auth_.hash_code(code), expires);
    std::cerr << "[displayd] pairing window opened\n";
}

} // namespace braillatron::display
