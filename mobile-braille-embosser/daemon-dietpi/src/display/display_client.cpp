#include "display_client.h"

#include "../connect/json_utils.h"

#include <cstring>
#include <poll.h>
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

    // displayd handles commands on its poll loop (~20 ms); avoid blocking forever.
    std::string response;
    char buffer[8192];
    pollfd pfd {fd, POLLIN, 0};
    for (int attempt = 0; attempt < 100 && response.empty(); ++attempt) {
        const int ready = poll(&pfd, 1, 20);
        if (ready < 0) {
            break;
        }
        if (ready == 0) {
            continue;
        }
        const ssize_t n = recv(fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            break;
        }
        buffer[n] = '\0';
        response += buffer;
    }
    close(fd);
    if (response.empty()) {
        return "{\"ok\":false,\"error\":\"displayd timeout\"}";
    }
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
