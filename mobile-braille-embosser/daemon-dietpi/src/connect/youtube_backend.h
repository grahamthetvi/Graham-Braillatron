#pragma once

#include "connect_config.h"
#include "event_writer.h"
#include "mpv_service.h"

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
    YoutubeBackend(YoutubeConfig config, ConnectConfig connect_config, MpvService *mpv,
                   EventWriter *events);

    bool cookies_present() const;
    void poll_cookie_import();
    std::string search(const std::string &query);
    std::string play(const std::string &url);
    std::string pause_toggle();
    std::string stop();

private:
    std::string cookie_args() const;
    std::string ytdlp_cookie_args() const;

    YoutubeConfig config_;
    ConnectConfig connect_config_;
    MpvService *mpv_;
    EventWriter *events_;
    bool paused_ = false;
};

} // namespace braillatron::connect
