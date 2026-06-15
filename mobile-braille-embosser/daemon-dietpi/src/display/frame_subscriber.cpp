#include "frame_subscriber.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace braillatron::display {

FrameSubscriber::FrameSubscriber(std::string socket_path)
    : socket_path_(std::move(socket_path))
{
}

FrameSubscriber::~FrameSubscriber()
{
    close();
}

bool FrameSubscriber::listen()
{
    listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        return false;
    }

    sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    if (socket_path_.size() >= sizeof(addr.sun_path)) {
        return false;
    }
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);
    unlink(socket_path_.c_str());

    if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[displayd] frame bind failed: " << std::strerror(errno) << "\n";
        return false;
    }
    if (::listen(listen_fd_, 4) < 0) {
        return false;
    }
    return true;
}

bool FrameSubscriber::read_full(int fd, uint8_t *buffer, size_t size)
{
    size_t offset = 0;
    while (offset < size) {
        const ssize_t n = recv(fd, buffer + offset, size - offset, 0);
        if (n <= 0) {
            return false;
        }
        offset += static_cast<size_t>(n);
    }
    return true;
}

void FrameSubscriber::poll_once()
{
    if (listen_fd_ < 0) {
        return;
    }

    if (client_fd_ < 0) {
        client_fd_ = accept(listen_fd_, nullptr, nullptr);
        if (client_fd_ < 0) {
            return;
        }
    }

    uint8_t header_prefix[17];
    if (!read_full(client_fd_, header_prefix, sizeof(header_prefix))) {
        ::close(client_fd_);
        client_fd_ = -1;
        return;
    }

    if (std::memcmp(header_prefix, kFrameMagic, 4) != 0 || header_prefix[4] != kFrameProtocolVersion) {
        ::close(client_fd_);
        client_fd_ = -1;
        return;
    }

    FrameHeader header {};
    std::memcpy(&header.frame_id, header_prefix + 5, sizeof(header.frame_id));
    std::memcpy(&header.width, header_prefix + 9, sizeof(header.width));
    std::memcpy(&header.height, header_prefix + 11, sizeof(header.height));
    std::memcpy(&header.crc32, header_prefix + 13, sizeof(header.crc32));

    const size_t pixel_count = static_cast<size_t>(header.width) * header.height;
    std::vector<uint16_t> pixels(pixel_count);
    if (!pixels.empty() &&
        !read_full(client_fd_, reinterpret_cast<uint8_t *>(pixels.data()),
                   pixel_count * sizeof(uint16_t))) {
        ::close(client_fd_);
        client_fd_ = -1;
        return;
    }

    if (header.crc32 != 0 && crc32_rgb565(pixels.data(), pixel_count) != header.crc32) {
        ::close(client_fd_);
        client_fd_ = -1;
        return;
    }

    ::close(client_fd_);
    client_fd_ = -1;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        latest_header_ = header;
        latest_pixels_ = std::move(pixels);
        has_frame_ = true;
    }

    if (callback_) {
        callback_(latest_header_, latest_pixels_);
    }
}

void FrameSubscriber::close()
{
    if (client_fd_ >= 0) {
        ::close(client_fd_);
        client_fd_ = -1;
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!socket_path_.empty()) {
        unlink(socket_path_.c_str());
    }
}

} // namespace braillatron::display
