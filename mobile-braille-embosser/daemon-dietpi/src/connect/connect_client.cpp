#include "connect_client.h"

#include "connect_async.h"
#include "json_utils.h"

#include <cctype>
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
    // Start at EOF so we only see events emitted after this process starts
    // (avoids replaying historical weather alerts on every UI restart).
    std::ifstream in(event_path_);
    if (in.is_open()) {
        in.seekg(0, std::ios::end);
        const auto pos = in.tellg();
        if (pos > 0) {
            event_offset_ = static_cast<size_t>(pos);
        }
    }
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

        // EventWriter should emit one JSON object per line. If a legacy multi-line
        // payload was written, keep reading until braces balance.
        auto brace_balance = [](const std::string &text) {
            int depth = 0;
            bool in_string = false;
            bool escape = false;
            for (char ch : text) {
                if (in_string) {
                    if (escape) {
                        escape = false;
                    } else if (ch == '\\') {
                        escape = true;
                    } else if (ch == '"') {
                        in_string = false;
                    }
                    continue;
                }
                if (ch == '"') {
                    in_string = true;
                } else if (ch == '{') {
                    ++depth;
                } else if (ch == '}') {
                    --depth;
                }
            }
            return depth;
        };

        while (brace_balance(line) > 0) {
            std::string more;
            if (!std::getline(in, more)) {
                break;
            }
            event_offset_ += more.size() + 1;
            line.push_back('\n');
            line += more;
        }

        ConnectEvent event;
        event.type = json_get_string(line, "event");
        const size_t data_pos = line.find("\"data\":");
        if (data_pos != std::string::npos) {
            size_t start = data_pos + 7;
            while (start < line.size() &&
                   std::isspace(static_cast<unsigned char>(line[start]))) {
                ++start;
            }
            if (start < line.size() && line[start] == '"') {
                event.data_json = json_get_string(line, "data");
            } else if (start < line.size() && line[start] == '{') {
                int depth = 0;
                bool in_string = false;
                bool escape = false;
                for (size_t i = start; i < line.size(); ++i) {
                    const char ch = line[i];
                    if (in_string) {
                        if (escape) {
                            escape = false;
                        } else if (ch == '\\') {
                            escape = true;
                        } else if (ch == '"') {
                            in_string = false;
                        }
                        continue;
                    }
                    if (ch == '"') {
                        in_string = true;
                        continue;
                    }
                    if (ch == '{') {
                        ++depth;
                    } else if (ch == '}') {
                        --depth;
                        if (depth == 0) {
                            event.data_json = line.substr(start, i - start + 1);
                            break;
                        }
                    }
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
