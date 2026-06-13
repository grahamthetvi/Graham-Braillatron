#pragma once

#include "mpv_ipc.h"
#include "subprocess.h"

#include <string>

namespace braillatron::connect {

class MpvService {
public:
    struct Options {
        std::string mpv_path = "mpv";
        std::string mpv_ao = "pulse";
        std::string socket_path;
        std::string extra_args;
    };

    explicit MpvService(Options options);

    bool ensure_started();
    void stop();
    MpvIpc &ipc() { return mpv_; }
    const MpvIpc &ipc() const { return mpv_; }

    bool pause_toggle();
    void mark_playing();
    bool is_paused() const { return paused_; }

private:
    Options options_;
    MpvIpc mpv_;
    ManagedProcess mpv_proc_;
    bool paused_ = false;
};

} // namespace braillatron::connect
