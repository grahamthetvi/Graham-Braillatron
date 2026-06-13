#include "connect_client.h"

#include "connect_async.h"
#include "json_utils.h"

#include <cstring>
#include <fstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace braillatron::connect {

ConnectClient::ConnectClient(std::string socket_path, std::string event_path)
    : socket_path_(std::move(socket_path))
    , event_path_(std::move(event_path))
{
}

std::string ConnectClient::request_with_payload(const std::string &payload)
{
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        connected_ = false;
        return "{\"ok\":false,\"error\":\"socket failed\"}";
    }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (socket_path_.size() >= sizeof(addr.sun_path)) {
        close(fd);
        return "{\"ok\":false,\"error\":\"path too long\"}";
    }
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(fd);
        connected_ = false;
        return "{\"ok\":false,\"error\":\"connectd unavailable\"}";
    }

    const std::string line = payload.back() == '\n' ? payload : payload + "\n";
    send(fd, line.c_str(), line.size(), 0);

    std::string response;
    char buffer[8192];
    ssize_t n = 0;
    while ((n = recv(fd, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[n] = '\0';
        response += buffer;
    }
    close(fd);
    connected_ = json_get_bool(response, "ok", false) ||
                 json_get_string(response, "service") == "connectd";
    return response;
}

std::string ConnectClient::request(const std::string &cmd, const std::string &extra_fields)
{
    std::string payload = "{\"cmd\":\"" + json_escape(cmd) + "\"";
    if (!extra_fields.empty()) {
        if (extra_fields.front() == ',') {
            payload += extra_fields;
        } else {
            payload += "," + extra_fields;
        }
    }
    payload += "}";
    return request_with_payload(payload);
}

void ConnectClient::request_async(const std::string &cmd, const std::string &extra_fields,
                                  AsyncCallback callback)
{
    const std::string request_id = generate_request_id();
    std::string payload = "{\"cmd\":\"" + json_escape(cmd) + "\",\"request_id\":\"" +
                          json_escape(request_id) + "\"";
    if (!extra_fields.empty()) {
        if (extra_fields.front() == ',') {
            payload += extra_fields;
        } else {
            payload += "," + extra_fields;
        }
    }
    payload += "}";

    const std::string response = request_with_payload(payload);
    if (!json_get_bool(response, "pending", false)) {
        if (callback) {
            callback(response);
        }
        return;
    }

    if (callback) {
        pending_[request_id] = std::move(callback);
    }
}

bool ConnectClient::ping()
{
    const std::string response = request("ping");
    return json_get_string(response, "service") == "connectd" ||
           json_get_bool(response, "ok", false);
}

void ConnectClient::poll_events(const std::function<void(const ConnectEvent &)> &handler)
{
    std::ifstream in(event_path_);
    if (!in.is_open()) {
        return;
    }
    in.seekg(static_cast<std::streamoff>(event_offset_));
    std::string line;
    while (std::getline(in, line)) {
        event_offset_ += line.size() + 1;
        if (line.empty()) {
            continue;
        }
        ConnectEvent event;
        event.type = json_get_string(line, "event");
        const size_t data_pos = line.find("\"data\":");
        if (data_pos != std::string::npos) {
            size_t start = data_pos + 7;
            if (start < line.size() && line[start] == '"') {
                event.data_json = json_get_string(line, "data");
            } else {
                const size_t end = line.find_last_of('}');
                if (end != std::string::npos && start < end) {
                    event.data_json = line.substr(start, end - start + 1);
                }
            }
        }
        if (!event.type.empty()) {
            dispatch_async_response(event);
            handler(event);
        }
    }
}

void ConnectClient::dispatch_async_response(const ConnectEvent &event)
{
    if (event.type != "connect.response") {
        return;
    }
    const std::string request_id = json_get_string(event.data_json, "request_id");
    if (request_id.empty()) {
        return;
    }
    const auto it = pending_.find(request_id);
    if (it == pending_.end()) {
        return;
    }
    AsyncCallback callback = std::move(it->second);
    pending_.erase(it);
    if (callback) {
        callback(event.data_json);
    }
}

void ConnectClient::register_pending_for_test(const std::string &request_id, AsyncCallback callback)
{
    if (callback) {
        pending_[request_id] = std::move(callback);
    }
}

} // namespace braillatron::connect
