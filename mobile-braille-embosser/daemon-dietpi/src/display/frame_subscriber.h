#pragma once

#include "frame_protocol.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace braillatron::display {

class FrameSubscriber {
public:
    using FrameCallback = std::function<void(const FrameHeader &, const std::vector<uint16_t> &)>;

    explicit FrameSubscriber(std::string socket_path);
    ~FrameSubscriber();

    bool listen();
    void poll_once();
    void close();

    void set_callback(FrameCallback callback) { callback_ = std::move(callback); }

    const FrameHeader &latest_header() const { return latest_header_; }
    const std::vector<uint16_t> &latest_pixels() const { return latest_pixels_; }
    bool has_frame() const { return has_frame_; }

private:
    bool read_full(int fd, uint8_t *buffer, size_t size);

    std::string socket_path_;
    int listen_fd_ = -1;
    int client_fd_ = -1;
    FrameCallback callback_;
    FrameHeader latest_header_ {};
    std::vector<uint16_t> latest_pixels_;
    bool has_frame_ = false;
    std::mutex mutex_;
};

} // namespace braillatron::display
