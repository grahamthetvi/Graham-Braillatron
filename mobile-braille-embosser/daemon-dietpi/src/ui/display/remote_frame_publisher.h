#pragma once

#include "chrome_frame.h"
#include "ui_chrome_model.h"

#include <cstdint>
#include <string>

namespace braillatron::ui {

inline bool should_publish_remote_frame(uint32_t crc32, uint32_t last_crc32)
{
    return crc32 != last_crc32;
}

class RemoteFramePublisher {
public:
    explicit RemoteFramePublisher(std::string socket_path);

    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

    void publish(const UiChromeModel &model);

private:
    bool should_publish(uint32_t crc32);
    bool send_packet(const std::vector<uint8_t> &packet);

    std::string socket_path_;
    bool enabled_ = false;
    bool connect_failed_logged_ = false;
    uint32_t frame_id_ = 0;
    uint32_t last_crc32_ = 0;
    ChromeRenderer renderer_;
    ChromeRasterizer rasterizer_;
    DisplaySurfaceLayout layout_;
};

} // namespace braillatron::ui
