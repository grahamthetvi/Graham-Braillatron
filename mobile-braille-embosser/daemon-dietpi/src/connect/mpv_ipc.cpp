#include "mpv_ipc.h"

#include "json_utils.h"

#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace braillatron::connect {

MpvIpc::MpvIpc(std::string socket_path)
    : socket_path_(std::move(socket_path))
{
}

bool MpvIpc::can_connect() const
{
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (socket_path_.size() >= sizeof(addr.sun_path)) {
        close(fd);
        return false;
    }
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    const bool ok = ::connect(fd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) == 0;
    close(fd);
    return ok;
}

bool MpvIpc::send_command(const std::string &command_json)
{
    std::string payload = command_json;
    if (payload.empty() || payload.back() != '\n') {
        payload += '\n';
    }

    for (int attempt = 0; attempt < 5; ++attempt) {
        const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            return false;
        }

        sockaddr_un addr {};
        addr.sun_family = AF_UNIX;
        if (socket_path_.size() >= sizeof(addr.sun_path)) {
            close(fd);
            return false;
        }
        std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

        if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
            close(fd);
            if (attempt + 1 < 5) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            return false;
        }

        size_t offset = 0;
        while (offset < payload.size()) {
            const ssize_t sent =
                send(fd, payload.c_str() + offset, payload.size() - offset, 0);
            if (sent <= 0) {
                close(fd);
                if (attempt + 1 < 5) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    break;
                }
                return false;
            }
            offset += static_cast<size_t>(sent);
        }
        if (offset >= payload.size()) {
            close(fd);
            return true;
        }
    }
    return false;
}

bool MpvIpc::load_url(const std::string &url)
{
    const std::string cmd = "{\"command\":[\"loadfile\",\"" + json_escape(url) + "\",\"replace\"]}";
    playing_ = send_command(cmd);
    return playing_;
}

bool MpvIpc::seek_seconds(double seconds)
{
    return send_command("{\"command\":[\"seek\"," + std::to_string(seconds) + ",\"absolute\"]}");
}

bool MpvIpc::seek_relative(double delta_seconds)
{
    return send_command("{\"command\":[\"seek\"," + std::to_string(delta_seconds) + ",\"relative\"]}");
}

bool MpvIpc::pause()
{
    playing_ = false;
    return send_command("{\"command\":[\"set_property\",\"pause\",true]}");
}

bool MpvIpc::resume()
{
    playing_ = true;
    return send_command("{\"command\":[\"set_property\",\"pause\",false]}");
}

bool MpvIpc::stop()
{
    playing_ = false;
    return send_command("{\"command\":[\"stop\"]}");
}

} // namespace braillatron::connect
