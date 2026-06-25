#include "remote_frame_publisher.h"

#include "../../display/frame_protocol.h"

#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace braillatron::ui {

RemoteFramePublisher::RemoteFramePublisher(std::string socket_path)
    : socket_path_(std::move(socket_path))
    , renderer_(max_body_rows_for_layout(layout_for_panel(240, 240)))
    , layout_(layout_for_panel(240, 240))
{
}

bool RemoteFramePublisher::should_publish(uint32_t crc32)
{
    return should_publish_remote_frame(crc32, last_crc32_);
}

bool RemoteFramePublisher::send_packet(const std::vector<uint8_t> &packet)
{
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        if (!connect_failed_logged_) {
            std::cerr << "[remote-display] socket create failed\n";
            connect_failed_logged_ = true;
        }
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
        if (!connect_failed_logged_) {
            std::cerr << "[remote-display] displayd unavailable; dropping frames\n";
            connect_failed_logged_ = true;
        }
        close(fd);
        return false;
    }

    connect_failed_logged_ = false;
    const ssize_t sent = send(fd, packet.data(), packet.size(), 0);
    close(fd);
    return sent == static_cast<ssize_t>(packet.size());
}

void RemoteFramePublisher::publish(const UiChromeModel &model)
{
    if (!enabled_) {
        return;
    }

    const ChromeFrame frame = rasterize_chrome(model, layout_, renderer_, rasterizer_);
    const uint32_t crc32 =
        braillatron::display::crc32_rgb565(frame.pixels.data(), frame.pixels.size());
    if (!should_publish(crc32)) {
        return;
    }

    braillatron::display::FrameHeader header;
    header.frame_id = ++frame_id_;
    header.width = layout_.width;
    header.height = layout_.height;
    header.crc32 = crc32;

    const auto packet =
        braillatron::display::encode_frame_packet(header, frame.pixels.data(), frame.pixels.size());
    if (send_packet(packet)) {
        last_crc32_ = crc32;
    }
}

} // namespace braillatron::ui
