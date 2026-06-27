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
    std::string channel;
    std::string duration;
};

class YoutubeBackend {
public:
    YoutubeBackend(YoutubeConfig config, ConnectConfig connect_config, MpvService *mpv,
                   EventWriter *events);

    static std::string mpv_extra_args(const YoutubeConfig &config);
    bool cookies_present() const;
    void poll_cookie_import();
    std::string search(const std::string &query);
    std::string recommended();
    std::string shorts();
    std::string play(const std::string &url);
    std::string pause_toggle();
    std::string stop();

private:
    std::string cookie_args() const;
    std::string ytdlp_cookie_args() const;
    std::string resolve_stream_url(const std::string &url) const;
    std::vector<YoutubeResult> fetch_playlist(const std::string &source, uint32_t limit) const;
    std::string format_results(const std::vector<YoutubeResult> &results, const std::string &feed,
                               bool personalized) const;

    YoutubeConfig config_;
    ConnectConfig connect_config_;
    MpvService *mpv_;
    EventWriter *events_;
    bool paused_ = false;
};

} // namespace braillatron::connect
