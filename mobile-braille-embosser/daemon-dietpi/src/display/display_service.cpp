#include "display_service.h"

#include "../connect/json_utils.h"

#include <iostream>
#include <sstream>

namespace braillatron::display {

DisplayService::DisplayService(RemoteDisplayConfig config)
    : config_(std::move(config))
    , auth_(config_.session_idle_minutes)
    , frame_subscriber_(config_.frame_socket_path)
    , cmd_server_(config_.cmd_socket_path)
{
    auth_.set_pairing_code_hash(config_.pairing_code_hash);
}

void DisplayService::apply_network_bind()
{
    if (config_.allow_lan) {
        config_.listen_address = "0.0.0.0";
    } else if (config_.listen_address.empty() || config_.listen_address == "0.0.0.0") {
        config_.listen_address = "127.0.0.1";
    }
}

bool DisplayService::start()
{
    apply_network_bind();

    if (!frame_subscriber_.listen()) {
        std::cerr << "[displayd] frame subscriber failed\n";
        return false;
    }

    frame_subscriber_.set_callback([this](const FrameHeader &header,
                                          const std::vector<uint16_t> &pixels) {
        if (http_server_ != nullptr) {
            http_server_->broadcast_frame(header, pixels);
        }
    });

    if (!cmd_server_.listen()) {
        std::cerr << "[displayd] command socket failed\n";
        return false;
    }

    running_ = true;

    if (!config_.enabled) {
        std::cerr << "[displayd] disabled by config (command socket ready)\n";
        return true;
    }

    virtual_keyboard_.init();

    http_server_ = std::make_unique<HttpServer>(config_.listen_address, config_.listen_port,
                                                config_.static_root, &auth_, &virtual_keyboard_);
    if (!http_server_->start()) {
        std::cerr << "[displayd] HTTP server failed\n";
        return false;
    }

    std::cerr << "[displayd] listening on " << config_.listen_address << ":" << config_.listen_port
              << "\n";
    return true;
}

void DisplayService::stop()
{
    running_ = false;
    cmd_server_.close();
    frame_subscriber_.close();
    if (http_server_ != nullptr) {
        http_server_->stop();
        http_server_.reset();
    }
    virtual_keyboard_.destroy();
}

void DisplayService::poll()
{
    if (!running_) {
        return;
    }
    frame_subscriber_.poll_once();
    cmd_server_.poll_once([this](const std::string &request) { return handle_command(request); });
    if (http_server_ != nullptr) {
        http_server_->poll_clients();
    }
}

std::string DisplayService::handle_command(const std::string &request)
{
    const std::string cmd = connect::json_get_string(request, "cmd");
    if (cmd == "ping") {
        return "{\"ok\":true,\"service\":\"displayd\"}";
    }
    if (cmd == "speak") {
        const std::string text = connect::json_get_string(request, "text");
        if (http_server_) {
            http_server_->broadcast_speak(text);
        }
        return "{\"ok\":true}";
    }
    if (cmd == "status") {
        std::ostringstream out;
        out << "{\"ok\":true,\"enabled\":" << (config_.enabled ? "true" : "false")
            << ",\"clients\":" << (http_server_ ? http_server_->connected_clients() : 0)
            << ",\"pairing_active\":" << (auth_.pairing_active() ? "true" : "false") << "}";
        return out.str();
    }
    if (cmd == "service.start") {
        config_.enabled = true;
        save_remote_display_config(remote_display_config_path_from_env(), config_);
        apply_network_bind();
        if (!http_server_) {
            http_server_ = std::make_unique<HttpServer>(config_.listen_address, config_.listen_port,
                                                        config_.static_root, &auth_, &virtual_keyboard_);
        }
        if (!http_server_->running() && !http_server_->start()) {
            return "{\"ok\":false,\"error\":\"http start failed\"}";
        }
        return "{\"ok\":true}";
    }
    if (cmd == "service.stop") {
        config_.enabled = false;
        save_remote_display_config(remote_display_config_path_from_env(), config_);
        if (http_server_) {
            http_server_->stop();
            http_server_.reset();
        }
        return "{\"ok\":true}";
    }
    if (cmd == "pairing.start") {
        const std::string code = auth_.start_pairing();
        return "{\"ok\":true,\"code\":\"" + connect::json_escape(code) + "\"}";
    }
    if (cmd == "pairing.stop") {
        auth_.stop_pairing();
        return "{\"ok\":true}";
    }
    if (cmd == "config.set") {
        if (connect::json_get_bool(request, "enabled", config_.enabled)) {
            // keep current
        }
        const std::string allow = connect::json_get_string(request, "allow_lan");
        if (!allow.empty()) {
            config_.allow_lan = connect::json_get_bool(request, "allow_lan", config_.allow_lan);
        }
        save_remote_display_config(remote_display_config_path_from_env(), config_);
        apply_network_bind();
        return "{\"ok\":true}";
    }
    if (cmd == "config.get") {
        std::ostringstream out;
        out << "{\"ok\":true,\"enabled\":" << (config_.enabled ? "true" : "false")
            << ",\"allow_lan\":" << (config_.allow_lan ? "true" : "false")
            << ",\"listen_port\":" << config_.listen_port << "}";
        return out.str();
    }
    return "{\"ok\":false,\"error\":\"unknown cmd\"}";
}

} // namespace braillatron::display
