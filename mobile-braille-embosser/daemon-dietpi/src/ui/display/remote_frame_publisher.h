#pragma once

#include "ui_chrome_model.h"

#include <cstdint>
#include <string>

namespace braillatron::ui {

class RemoteFramePublisher {
public:
    explicit RemoteFramePublisher(std::string socket_path);

    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

    void publish(const UiChromeModel &model);

private:
    bool ensure_connected();
    void disconnect();

    std::string socket_path_;
    bool enabled_ = false;
    int socket_fd_ = -1;
    uint32_t frame_id_ = 0;
    bool connect_warned_ = false;
};

} // namespace braillatron::ui
