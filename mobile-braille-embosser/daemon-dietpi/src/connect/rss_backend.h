#pragma once

#include "connect_config.h"
#include "event_writer.h"
#include "mpv_service.h"

#include <cstdint>
#include <string>
#include <vector>

namespace braillatron::connect {

struct PodcastEpisode {
    std::string id;
    std::string feed_id;
    std::string title;
    std::string enclosure_url;
    std::string pub_date;
    std::string local_path;
    bool downloaded = false;
};

struct PodcastFeed {
    std::string id;
    std::string title;
    std::string url;
    std::string description;
    uint64_t last_refreshed = 0;
    std::vector<PodcastEpisode> episodes;
};

// RSS/Atom/OPML parsing helpers (used by self-tests).
std::string xml_tag_content(const std::string &xml, const std::string &tag);
std::string xml_attr_value(const std::string &xml, const std::string &tag, const std::string &attr);
std::vector<std::string> xml_blocks(const std::string &xml, const std::string &tag);
std::vector<PodcastFeed> parse_opml(const std::string &opml);
std::vector<PodcastEpisode> parse_rss_episodes(const std::string &xml, const std::string &feed_id,
                                               uint32_t max_episodes);

class RssBackend {
public:
    RssBackend(PodcastsConfig config, ConnectConfig connect_config, MpvService *mpv,
               EventWriter *events);

    std::string list_feeds();
    std::string refresh();
    std::string list_episodes(const std::string &feed_id);
    std::string download(const std::string &episode_id);
    std::string play(const std::string &episode_id);
    std::string pause_toggle();
    std::string stop();
    std::string status() const;
    std::string import_opml();

    void poll_refresh(uint64_t now_sec);

private:
    bool load_store();
    bool save_store() const;
    bool fetch_feed(PodcastFeed &feed);
    std::string curl_fetch(const std::string &url) const;
    bool download_episode_file(PodcastEpisode &episode, const PodcastFeed &feed);
    const PodcastEpisode *find_episode(const std::string &episode_id) const;
    PodcastEpisode *find_episode_mut(const std::string &episode_id);
    std::string episode_event_json(const PodcastEpisode &episode) const;
    std::string play_episode(PodcastEpisode &episode);

    PodcastsConfig config_;
    ConnectConfig connect_config_;
    MpvService *mpv_;
    EventWriter *events_;
    std::vector<PodcastFeed> feeds_;
    int current_episode_index_ = -1;
    std::string current_episode_id_;
    bool paused_ = false;
    uint64_t last_poll_refresh_sec_ = 0;
};

} // namespace braillatron::connect
