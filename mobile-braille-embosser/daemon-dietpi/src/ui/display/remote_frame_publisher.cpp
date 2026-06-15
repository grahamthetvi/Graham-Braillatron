#include "remote_frame_publisher.h"

#include "chrome_frame.h"
#include "../../display/frame_protocol.h"

#include <chrono>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace braillatron::ui {

namespace {

constexpr auto kMinFrameInterval = std::chrono::milliseconds(100);

} // namespace

RemoteFramePublisher::RemoteFramePublisher(std::string socket_path)
    : socket_path_(std::move(socket_path))
{
}

bool RemoteFramePublisher::ensure_connected()
{
    if (socket_fd_ >= 0) {
        return true;
    }

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

    if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(fd);
        if (!connect_warned_) {
            std::cerr << "[display-remote] displayd not reachable at " << socket_path_
                      << " (frames dropped until connected)\n";
            connect_warned_ = true;
        }
        return false;
    }

    connect_warned_ = false;
    socket_fd_ = fd;
    return true;
}

void RemoteFramePublisher::disconnect()
{
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

void RemoteFramePublisher::publish(const UiChromeModel &model)
{
    if (!enabled_) {
        return;
    }

    static auto last_sent = std::chrono::steady_clock::time_point {};
    const auto now = std::chrono::steady_clock::now();
    if (last_sent != std::chrono::steady_clock::time_point {} &&
        now - last_sent < kMinFrameInterval) {
        return;
    }

    if (!ensure_connected()) {
        return;
    }

    const ChromeFrame frame = rasterize_chrome_panel(model);
    braillatron::display::FrameHeader header {};
    header.frame_id = ++frame_id_;
    header.width = frame.layout.width;
    header.height = frame.layout.height;
    header.payload_bytes =
        static_cast<uint32_t>(frame.pixels.size() * sizeof(uint16_t));

    std::vector<uint8_t> packet(sizeof(braillatron::display::FrameHeader) +
                                frame.pixels.size() * sizeof(uint16_t));
    std::memcpy(packet.data(), &header, sizeof(header));
    std::memcpy(packet.data() + sizeof(header), frame.pixels.data(),
                frame.pixels.size() * sizeof(uint16_t));

    size_t offset = 0;
    while (offset < packet.size()) {
        const ssize_t written =
            send(socket_fd_, packet.data() + offset, packet.size() - offset, MSG_NOSIGNAL);
        if (written <= 0) {
            disconnect();
            return;
        }
        offset += static_cast<size_t>(written);
    }

    last_sent = now;
}

} // namespace braillatron::ui
