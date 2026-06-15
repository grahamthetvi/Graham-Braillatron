#include "remote_display_client.h"

#include <cstring>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace braillatron::ui {

bool send_display_pairing_command(const std::string &cmd_socket, const std::string &code)
{
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (cmd_socket.size() >= sizeof(addr.sun_path)) {
        close(fd);
        return false;
    }
    std::strncpy(addr.sun_path, cmd_socket.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return false;
    }

    std::ostringstream stream;
    stream << "{\"cmd\":\"pairing.start\",\"code\":\"" << code << "\"}\n";
    const std::string payload = stream.str();
    const ssize_t sent = send(fd, payload.data(), payload.size(), MSG_NOSIGNAL);
    close(fd);
    return sent == static_cast<ssize_t>(payload.size());
}

} // namespace braillatron::ui
