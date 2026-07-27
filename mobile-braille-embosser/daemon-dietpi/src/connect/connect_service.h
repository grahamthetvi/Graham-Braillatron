#pragma once

#include "connect_config.h"
#include "connect_job_queue.h"
#include "event_writer.h"
#include "gmail_backend.h"
#include "library_backend.h"
#include "music_backend.h"
#include "mpv_service.h"
#include "radio_backend.h"
#include "rss_backend.h"
#include "signal_backend.h"
#include "socket_server.h"
#include "weather_backend.h"
#include "worthwhile_backend.h"
#include "youtube_backend.h"

#include <atomic>
#include <cstdint>

namespace braillatron::connect {

class ConnectService {
public:
    ConnectService(ConnectConfig connect_config, YoutubeConfig youtube_config,
                   MessagesConfig messages_config, MusicConfig music_config,
                   WeatherConfig weather_config, PodcastsConfig podcasts_config,
                   RadioConfig radio_config, LibraryConfig library_config,
                   WorthwhileConfig worthwhile_config, GmailConfig gmail_config);

    void start();
    void stop();
    void poll();

private:
    std::string handle_request(const std::string &request);
    std::string execute_command(const std::string &cmd, const std::string &request);
    std::string cmd_from_request(const std::string &request) const;
    std::string media_pause_toggle();
    std::string media_set_pause(bool pause);
    std::string media_skip(double delta_seconds);
    std::string media_stop();

    ConnectConfig connect_config_;
    EventWriter events_;
    MpvService mpv_;
    YoutubeBackend youtube_;
    MusicBackend music_;
    WeatherBackend weather_;
    RssBackend podcasts_;
    RadioBackend radio_;
    LibraryBackend library_;
    WorthwhileBackend worthwhile_;
    GmailBackend gmail_;
    SignalBackend signal_;
    SocketServer server_;
    ConnectJobQueue jobs_;
    std::atomic<bool> running_ {false};
    uint64_t last_cookie_poll_ms_ = 0;
    uint64_t last_radio_metadata_poll_ms_ = 0;
    uint64_t last_podcast_refresh_sec_ = 0;
};

} // namespace braillatron::connect
