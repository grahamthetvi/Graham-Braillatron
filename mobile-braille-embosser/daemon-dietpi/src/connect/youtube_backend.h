#pragma once

#include "connect_config.h"
#include "event_writer.h"
#include "mpv_ipc.h"
#include "subprocess.h"

#include <string>
#include <vector>

namespace braillatron::connect {

struct YoutubeResult {
    std::string id;
    std::string title;
    std::string url;
};

class YoutubeBackend {
public:
    YoutubeBackend(YoutubeConfig config, ConnectConfig connect_config, EventWriter *events);

    bool cookies_present() const;
    void poll_cookie_import();
    std::string search(const std::string &query);
    std::string play(const std::string &url);
    std::string pause_toggle();
    std::string stop();
    bool start_mpv();
    void stop_mpv();

private:
    std::string cookie_args() const;
    std::string ytdlp_cookie_args() const;

    YoutubeConfig config_;
    ConnectConfig connect_config_;
    EventWriter *events_;
    MpvIpc mpv_;
    ManagedProcess mpv_proc_;
    bool paused_ = false;
};

} // namespace braillatron::connect
