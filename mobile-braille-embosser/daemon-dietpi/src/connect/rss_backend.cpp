#include "rss_backend.h"

#include "json_utils.h"
#include "subprocess.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace braillatron::connect {

namespace fs = std::filesystem;

namespace {

constexpr const char *kUserAgent = "Braillatron/1.0 (accessibility device)";

uint64_t unix_now_sec()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

std::string sanitize_filename(const std::string &value)
{
    std::string out;
    for (char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_') {
            out += ch;
        } else if (ch == ' ') {
            out += '_';
        }
    }
    if (out.empty()) {
        return "episode";
    }
    if (out.size() > 64) {
        out.resize(64);
    }
    return out;
}

std::string extension_from_url(const std::string &url)
{
    const size_t query = url.find('?');
    const std::string path = query == std::string::npos ? url : url.substr(0, query);
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= path.size()) {
        return ".mp3";
    }
    const std::string ext = path.substr(dot);
    if (ext.size() > 6) {
        return ".mp3";
    }
    return ext;
}

} // namespace

std::string xml_tag_content(const std::string &xml, const std::string &tag)
{
    const std::string open = "<" + tag;
    const std::string close = "</" + tag + ">";
    size_t pos = xml.find(open);
    if (pos == std::string::npos) {
        return {};
    }
    pos = xml.find('>', pos);
    if (pos == std::string::npos) {
        return {};
    }
    ++pos;
    const size_t end = xml.find(close, pos);
    if (end == std::string::npos) {
        return {};
    }
    std::string content = xml.substr(pos, end - pos);
    const size_t cdata_start = content.find("<![CDATA[");
    if (cdata_start != std::string::npos) {
        const size_t cdata_end = content.find("]]>", cdata_start);
        if (cdata_end != std::string::npos) {
            return content.substr(cdata_start + 9, cdata_end - cdata_start - 9);
        }
    }
    size_t lt = 0;
    while ((lt = content.find('<', lt)) != std::string::npos) {
        const size_t gt = content.find('>', lt);
        if (gt == std::string::npos) {
            break;
        }
        content.erase(lt, gt - lt + 1);
    }
    return content;
}

std::string xml_attr_value(const std::string &xml, const std::string &tag, const std::string &attr)
{
    const std::string open = "<" + tag;
    size_t pos = xml.find(open);
    if (pos == std::string::npos) {
        return {};
    }
    const size_t tag_end = xml.find('>', pos);
    if (tag_end == std::string::npos) {
        return {};
    }
    const std::string tag_block = xml.substr(pos, tag_end - pos);
    const std::string needle = attr + "=\"";
    const size_t attr_pos = tag_block.find(needle);
    if (attr_pos == std::string::npos) {
        const std::string needle_single = attr + "='";
        const size_t single_pos = tag_block.find(needle_single);
        if (single_pos == std::string::npos) {
            return {};
        }
        const size_t start = single_pos + needle_single.size();
        const size_t end = tag_block.find('\'', start);
        if (end == std::string::npos) {
            return {};
        }
        return tag_block.substr(start, end - start);
    }
    const size_t start = attr_pos + needle.size();
    const size_t end = tag_block.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return tag_block.substr(start, end - start);
}

std::vector<std::string> xml_blocks(const std::string &xml, const std::string &tag)
{
    std::vector<std::string> blocks;
    const std::string open = "<" + tag;
    const std::string close = "</" + tag + ">";
    size_t pos = 0;
    while (pos < xml.size()) {
        const size_t start = xml.find(open, pos);
        if (start == std::string::npos) {
            break;
        }
        const size_t end = xml.find(close, start);
        if (end == std::string::npos) {
            break;
        }
        blocks.push_back(xml.substr(start, end - start + close.size()));
        pos = end + close.size();
    }
    return blocks;
}

std::vector<PodcastFeed> parse_opml(const std::string &opml)
{
    std::vector<PodcastFeed> feeds;
    size_t pos = 0;
    while (pos < opml.size()) {
        const size_t outline = opml.find("<outline", pos);
        if (outline == std::string::npos) {
            break;
        }
        const size_t end = opml.find('>', outline);
        if (end == std::string::npos) {
            break;
        }
        const std::string block = opml.substr(outline, end - outline + 1);
        const std::string feed_url = xml_attr_value(block, "outline", "xmlUrl");
        if (!feed_url.empty()) {
            PodcastFeed feed;
            feed.id = std::to_string(feeds.size());
            feed.title = xml_attr_value(block, "outline", "title");
            if (feed.title.empty()) {
                feed.title = xml_attr_value(block, "outline", "text");
            }
            if (feed.title.empty()) {
                feed.title = "Podcast " + feed.id;
            }
            feed.url = feed_url;
            feeds.push_back(std::move(feed));
        }
        pos = end + 1;
    }
    return feeds;
}

std::vector<PodcastEpisode> parse_rss_episodes(const std::string &xml, const std::string &feed_id,
                                               uint32_t max_episodes)
{
    std::vector<PodcastEpisode> episodes;
    const bool is_atom = xml.find("<feed") != std::string::npos;
    const std::string block_tag = is_atom ? "entry" : "item";
    const std::vector<std::string> blocks = xml_blocks(xml, block_tag);

    for (size_t i = 0; i < blocks.size() && episodes.size() < max_episodes; ++i) {
        const std::string &block = blocks[i];
        PodcastEpisode episode;
        episode.feed_id = feed_id;
        episode.id = feed_id + "-" + std::to_string(i);
        episode.title = xml_tag_content(block, "title");
        if (episode.title.empty()) {
            continue;
        }

        if (is_atom) {
            episode.pub_date = xml_tag_content(block, "published");
            if (episode.pub_date.empty()) {
                episode.pub_date = xml_tag_content(block, "updated");
            }
            episode.enclosure_url = xml_attr_value(block, "link", "href");
            if (episode.enclosure_url.empty()) {
                const std::vector<std::string> links = xml_blocks(block, "link");
                for (const std::string &link : links) {
                    const std::string rel = xml_attr_value(link, "link", "rel");
                    const std::string type = xml_attr_value(link, "link", "type");
                    if (rel == "enclosure" || type.find("audio") != std::string::npos) {
                        episode.enclosure_url = xml_attr_value(link, "link", "href");
                        break;
                    }
                }
            }
        } else {
            episode.pub_date = xml_tag_content(block, "pubDate");
            episode.enclosure_url = xml_attr_value(block, "enclosure", "url");
            if (episode.enclosure_url.empty()) {
                episode.enclosure_url = xml_tag_content(block, "link");
            }
        }

        if (episode.enclosure_url.empty()) {
            continue;
        }
        episodes.push_back(std::move(episode));
    }
    return episodes;
}

RssBackend::RssBackend(PodcastsConfig config, ConnectConfig connect_config, MpvService *mpv,
                       EventWriter *events)
    : config_(std::move(config))
    , connect_config_(connect_config)
    , mpv_(mpv)
    , events_(events)
{
    load_store();
}

std::string RssBackend::curl_fetch(const std::string &url) const
{
    const std::string cmd = "curl -fsS --max-time 30 -A \"" + std::string(kUserAgent) + "\" \"" +
                            url + "\" 2>/dev/null";
    return run_command(cmd);
}

bool RssBackend::load_store()
{
    feeds_.clear();
    if (!file_exists(config_.feeds_path)) {
        return true;
    }
    std::ifstream in(config_.feeds_path);
    if (!in.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string json = buffer.str();

    const size_t feeds_pos = json.find("\"feeds\":[");
    if (feeds_pos == std::string::npos) {
        return true;
    }
    const size_t feeds_end = json.find(']', feeds_pos);
    const std::string feeds_array = json.substr(feeds_pos + 9, feeds_end - feeds_pos - 9);

    for (const std::string &feed_obj : json_split_objects("[" + feeds_array + "]")) {
        PodcastFeed feed;
        feed.id = json_get_string(feed_obj, "id");
        feed.title = json_get_string(feed_obj, "title");
        feed.url = json_get_string(feed_obj, "url");
        feed.description = json_get_string(feed_obj, "description");
        const std::string refreshed = json_get_string(feed_obj, "last_refreshed");
        feed.last_refreshed = refreshed.empty() ? 0 : std::stoull(refreshed);

        const size_t episodes_pos = feed_obj.find("\"episodes\":[");
        if (episodes_pos != std::string::npos) {
            const size_t episodes_end = feed_obj.find(']', episodes_pos);
            const std::string episodes_array =
                feed_obj.substr(episodes_pos + 12, episodes_end - episodes_pos - 12);
            for (const std::string &ep_obj : json_split_objects("[" + episodes_array + "]")) {
                PodcastEpisode episode;
                episode.id = json_get_string(ep_obj, "id");
                episode.feed_id = feed.id;
                episode.title = json_get_string(ep_obj, "title");
                episode.enclosure_url = json_get_string(ep_obj, "enclosure_url");
                episode.pub_date = json_get_string(ep_obj, "pub_date");
                episode.local_path = json_get_string(ep_obj, "local_path");
                episode.downloaded = json_get_bool(ep_obj, "downloaded", false);
                if (!episode.id.empty() && !episode.title.empty()) {
                    feed.episodes.push_back(std::move(episode));
                }
            }
        }
        if (!feed.id.empty() && !feed.url.empty()) {
            feeds_.push_back(std::move(feed));
        }
    }
    return true;
}

bool RssBackend::save_store() const
{
    const size_t slash = config_.feeds_path.find_last_of('/');
    if (slash != std::string::npos) {
        ensure_directory(config_.feeds_path.substr(0, slash));
    }

    std::ostringstream out;
    out << "{\n  \"feeds\": [\n";
    for (size_t fi = 0; fi < feeds_.size(); ++fi) {
        const PodcastFeed &feed = feeds_[fi];
        if (fi > 0) {
            out << ",\n";
        }
        out << "    {\n"
            << "      \"id\": \"" << json_escape(feed.id) << "\",\n"
            << "      \"title\": \"" << json_escape(feed.title) << "\",\n"
            << "      \"url\": \"" << json_escape(feed.url) << "\",\n"
            << "      \"description\": \"" << json_escape(feed.description) << "\",\n"
            << "      \"last_refreshed\": " << feed.last_refreshed << ",\n"
            << "      \"episodes\": [\n";
        for (size_t ei = 0; ei < feed.episodes.size(); ++ei) {
            const PodcastEpisode &ep = feed.episodes[ei];
            if (ei > 0) {
                out << ",\n";
            }
            out << "        {\n"
                << "          \"id\": \"" << json_escape(ep.id) << "\",\n"
                << "          \"title\": \"" << json_escape(ep.title) << "\",\n"
                << "          \"enclosure_url\": \"" << json_escape(ep.enclosure_url) << "\",\n"
                << "          \"pub_date\": \"" << json_escape(ep.pub_date) << "\",\n"
                << "          \"local_path\": \"" << json_escape(ep.local_path) << "\",\n"
                << "          \"downloaded\": " << (ep.downloaded ? "true" : "false") << "\n"
                << "        }";
        }
        out << "\n      ]\n    }";
    }
    out << "\n  ]\n}\n";

    const std::string temp_path = config_.feeds_path + ".part";
    std::ofstream file(temp_path);
    if (!file.is_open()) {
        return false;
    }
    file << out.str();
    file.close();
    return atomic_move_file(temp_path, config_.feeds_path);
}

bool RssBackend::fetch_feed(PodcastFeed &feed)
{
    const std::string xml = curl_fetch(feed.url);
    if (xml.empty()) {
        return false;
    }

    if (feed.title.empty() || feed.title.find("Podcast ") == 0) {
        const std::string channel_title = xml_tag_content(xml, "title");
        if (!channel_title.empty()) {
            feed.title = channel_title;
        }
    }
    feed.description = xml_tag_content(xml, "description");
    if (feed.description.empty()) {
        feed.description = xml_tag_content(xml, "subtitle");
    }

    std::vector<PodcastEpisode> fresh =
        parse_rss_episodes(xml, feed.id, config_.max_episodes_per_feed);

    for (PodcastEpisode &ep : fresh) {
        for (const PodcastEpisode &existing : feed.episodes) {
            if (existing.enclosure_url == ep.enclosure_url) {
                ep.local_path = existing.local_path;
                ep.downloaded = existing.downloaded;
                break;
            }
        }
    }
    feed.episodes = std::move(fresh);
    feed.last_refreshed = unix_now_sec();
    return true;
}

std::string RssBackend::import_opml()
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"podcasts disabled\"}";
    }

    ensure_directory(config_.import_dir);
    std::error_code ec;
    if (!fs::exists(config_.import_dir, ec)) {
        return "{\"ok\":true,\"imported\":0}";
    }

    int imported = 0;
    for (const auto &entry : fs::directory_iterator(config_.import_dir, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        const std::string path = entry.path().string();
        const std::string ext = entry.path().extension().string();
        if (ext != ".opml" && ext != ".xml") {
            continue;
        }

        std::ifstream in(path);
        if (!in.is_open()) {
            continue;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        const std::vector<PodcastFeed> new_feeds = parse_opml(buffer.str());

        for (const PodcastFeed &incoming : new_feeds) {
            bool exists = false;
            for (const PodcastFeed &existing : feeds_) {
                if (existing.url == incoming.url) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                PodcastFeed feed = incoming;
                feed.id = std::to_string(feeds_.size());
                feeds_.push_back(std::move(feed));
                ++imported;
            }
        }

        const std::string processed_dir = config_.import_dir + "/processed";
        ensure_directory(processed_dir);
        atomic_move_file(path, processed_dir + "/" + entry.path().filename().string());
    }

    save_store();
    return "{\"ok\":true,\"imported\":" + std::to_string(imported) + "}";
}

std::string RssBackend::list_feeds()
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"podcasts disabled\"}";
    }
    load_store();

    std::ostringstream out;
    out << "{\"ok\":true,\"feeds\":[";
    for (size_t i = 0; i < feeds_.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        const PodcastFeed &feed = feeds_[i];
        out << "{\"id\":\"" << json_escape(feed.id) << "\",\"title\":\"" << json_escape(feed.title)
            << "\",\"url\":\"" << json_escape(feed.url) << "\",\"episode_count\":"
            << feed.episodes.size() << ",\"last_refreshed\":" << feed.last_refreshed << "}";
    }
    out << "]}";
    return out.str();
}

std::string RssBackend::refresh()
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"podcasts disabled\"}";
    }

    import_opml();
    load_store();

    int refreshed = 0;
    for (PodcastFeed &feed : feeds_) {
        if (fetch_feed(feed)) {
            ++refreshed;
        }
    }
    save_store();

    return "{\"ok\":true,\"refreshed\":" + std::to_string(refreshed) +
           ",\"feed_count\":" + std::to_string(feeds_.size()) + "}";
}

std::string RssBackend::list_episodes(const std::string &feed_id)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"podcasts disabled\"}";
    }
    load_store();

    for (const PodcastFeed &feed : feeds_) {
        if (feed.id != feed_id) {
            continue;
        }
        std::ostringstream out;
        out << "{\"ok\":true,\"feed_id\":\"" << json_escape(feed.id) << "\",\"title\":\""
            << json_escape(feed.title) << "\",\"episodes\":[";
        for (size_t i = 0; i < feed.episodes.size(); ++i) {
            if (i > 0) {
                out << ',';
            }
            const PodcastEpisode &ep = feed.episodes[i];
            out << "{\"id\":\"" << json_escape(ep.id) << "\",\"title\":\"" << json_escape(ep.title)
                << "\",\"downloaded\":" << (ep.downloaded ? "true" : "false")
                << ",\"pub_date\":\"" << json_escape(ep.pub_date) << "\"}";
        }
        out << "]}";
        return out.str();
    }
    return "{\"ok\":false,\"error\":\"feed not found\"}";
}

bool RssBackend::download_episode_file(PodcastEpisode &episode, const PodcastFeed &feed)
{
    if (episode.enclosure_url.empty()) {
        return false;
    }

    const std::string feed_dir = config_.download_dir + "/" + sanitize_filename(feed.title);
    ensure_directory(feed_dir);

    const std::string filename =
        sanitize_filename(episode.title) + extension_from_url(episode.enclosure_url);
    const std::string dest = feed_dir + "/" + filename;
    const std::string temp = dest + ".part";

    const std::string cmd = "curl -fsS --max-time 600 -A \"" + std::string(kUserAgent) +
                            "\" -o \"" + temp + "\" \"" + episode.enclosure_url + "\" 2>/dev/null";
    if (run_command_status(cmd) != 0) {
        return false;
    }
    if (!atomic_move_file(temp, dest)) {
        return false;
    }
    episode.local_path = dest;
    episode.downloaded = true;
    save_store();
    return true;
}

std::string RssBackend::download(const std::string &episode_id)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"podcasts disabled\"}";
    }
    load_store();

    for (PodcastFeed &feed : feeds_) {
        for (PodcastEpisode &episode : feed.episodes) {
            if (episode.id != episode_id) {
                continue;
            }
            if (episode.downloaded && file_exists(episode.local_path)) {
                return "{\"ok\":true,\"already_downloaded\":true,\"path\":\"" +
                       json_escape(episode.local_path) + "\"}";
            }
            if (!download_episode_file(episode, feed)) {
                return "{\"ok\":false,\"error\":\"download failed\"}";
            }
            return "{\"ok\":true,\"path\":\"" + json_escape(episode.local_path) + "\"}";
        }
    }
    return "{\"ok\":false,\"error\":\"episode not found\"}";
}

const PodcastEpisode *RssBackend::find_episode(const std::string &episode_id) const
{
    for (const PodcastFeed &feed : feeds_) {
        for (const PodcastEpisode &episode : feed.episodes) {
            if (episode.id == episode_id) {
                return &episode;
            }
        }
    }
    return nullptr;
}

PodcastEpisode *RssBackend::find_episode_mut(const std::string &episode_id)
{
    for (PodcastFeed &feed : feeds_) {
        for (PodcastEpisode &episode : feed.episodes) {
            if (episode.id == episode_id) {
                return &episode;
            }
        }
    }
    return nullptr;
}

std::string RssBackend::episode_event_json(const PodcastEpisode &episode) const
{
    return "{\"id\":\"" + json_escape(episode.id) + "\",\"title\":\"" +
           json_escape(episode.title) + "\",\"feed_id\":\"" + json_escape(episode.feed_id) +
           "\",\"downloaded\":" + (episode.downloaded ? "true" : "false") + ",\"path\":\"" +
           json_escape(episode.local_path) + "\"}";
}

std::string RssBackend::play_episode(PodcastEpisode &episode)
{
    if (mpv_ == nullptr) {
        return "{\"ok\":false,\"error\":\"mpv unavailable\"}";
    }
    if (!mpv_->ensure_started()) {
        return "{\"ok\":false,\"error\":\"mpv start failed\"}";
    }

    std::string source = episode.local_path;
    if (source.empty() || !file_exists(source)) {
        source = episode.enclosure_url;
    }
    if (source.empty()) {
        return "{\"ok\":false,\"error\":\"no playable source\"}";
    }

    paused_ = false;
    current_episode_id_ = episode.id;
    mpv_->mark_playing();
    if (!mpv_->ipc().load_url(source)) {
        return "{\"ok\":false,\"error\":\"mpv load failed\"}";
    }

    if (events_ != nullptr) {
        events_->emit("podcasts.playing", episode_event_json(episode));
    }
    return "{\"ok\":true,\"episode\":" + episode_event_json(episode) + "}";
}

std::string RssBackend::play(const std::string &episode_id)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"podcasts disabled\"}";
    }
    load_store();
    PodcastEpisode *episode = find_episode_mut(episode_id);
    if (episode == nullptr) {
        return "{\"ok\":false,\"error\":\"episode not found\"}";
    }
    return play_episode(*episode);
}

std::string RssBackend::pause_toggle()
{
    if (mpv_ == nullptr) {
        return "{\"ok\":false,\"error\":\"mpv unavailable\"}";
    }
    mpv_->pause_toggle();
    paused_ = mpv_->is_paused();
    return "{\"ok\":true,\"paused\":" + std::string(paused_ ? "true" : "false") + "}";
}

std::string RssBackend::stop()
{
    if (mpv_ != nullptr) {
        mpv_->ipc().stop();
    }
    paused_ = false;
    current_episode_id_.clear();
    if (events_ != nullptr) {
        events_->emit("podcasts.ended", "{}");
    }
    return "{\"ok\":true}";
}

std::string RssBackend::status() const
{
    if (current_episode_id_.empty()) {
        return "{\"ok\":true,\"playing\":false}";
    }
    const PodcastEpisode *episode = find_episode(current_episode_id_);
    if (episode == nullptr) {
        return "{\"ok\":true,\"playing\":false}";
    }
    return "{\"ok\":true,\"playing\":" + std::string(paused_ ? "false" : "true") +
           ",\"paused\":" + std::string(paused_ ? "true" : "false") +
           ",\"episode\":" + episode_event_json(*episode) + "}";
}

void RssBackend::poll_refresh(uint64_t now_sec)
{
    if (!config_.enabled || config_.refresh_interval_sec == 0) {
        return;
    }
    if (last_poll_refresh_sec_ != 0 &&
        now_sec - last_poll_refresh_sec_ < config_.refresh_interval_sec) {
        return;
    }
    last_poll_refresh_sec_ = now_sec;
    refresh();
}

} // namespace braillatron::connect
