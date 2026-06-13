#include "mpv_ipc.h"

#include "json_utils.h"

#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace braillatron::connect {

MpvIpc::MpvIpc(std::string socket_path)
    : socket_path_(std::move(socket_path))
{
}

bool MpvIpc::send_command(const std::string &command_json)
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

    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    std::string payload = command_json;
    if (payload.empty() || payload.back() != '\n') {
        payload += '\n';
    }
    const ssize_t sent = send(fd, payload.c_str(), payload.size(), 0);
    close(fd);
    return sent > 0;
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
