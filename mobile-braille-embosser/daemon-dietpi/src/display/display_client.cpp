#include "display_client.h"

#include "../connect/json_utils.h"

#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace braillatron::display {

DisplayClient::DisplayClient(std::string socket_path)
    : socket_path_(std::move(socket_path))
{
}

std::string DisplayClient::request_with_payload(const std::string &payload)
{
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
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
        return "{\"ok\":false,\"error\":\"displayd unavailable\"}";
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
    return response;
}

std::string DisplayClient::request(const std::string &cmd, const std::string &extra_fields)
{
    std::string payload = "{\"cmd\":\"" + connect::json_escape(cmd) + "\"";
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

bool DisplayClient::ping()
{
    const std::string response = request("ping");
    return connect::json_get_string(response, "service") == "displayd";
}

} // namespace braillatron::display
