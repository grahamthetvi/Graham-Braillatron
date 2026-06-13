#include "music_backend.h"

#include "json_utils.h"
#include "subprocess.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace braillatron::connect {

namespace fs = std::filesystem;

namespace {

std::string lower_ext(const std::string &path)
{
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= path.size()) {
        return {};
    }
    std::string ext = path.substr(dot);
    for (char &ch : ext) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return ext;
}

std::string basename_no_ext(const std::string &path)
{
    const size_t slash = path.find_last_of('/');
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) {
        return name;
    }
    return name.substr(0, dot);
}

std::string parent_name(const std::string &path)
{
    std::error_code ec;
    const fs::path parent = fs::path(path).parent_path();
    if (parent.empty()) {
        return {};
    }
    return parent.filename().string();
}

std::string grandparent_name(const std::string &path)
{
    std::error_code ec;
    const fs::path parent = fs::path(path).parent_path();
    if (parent.empty()) {
        return {};
    }
    const fs::path grand = parent.parent_path();
    if (grand.empty()) {
        return {};
    }
    return grand.filename().string();
}

} // namespace

MusicBackend::MusicBackend(MusicConfig config, ConnectConfig connect_config, MpvService *mpv,
                           EventWriter *events)
    : config_(std::move(config))
    , connect_config_(connect_config)
    , mpv_(mpv)
    , events_(events)
{
}

bool MusicBackend::is_audio_extension(const std::string &ext) const
{
    for (const std::string &allowed : config_.extensions) {
        if (ext == allowed) {
            return true;
        }
    }
    return false;
}

void MusicBackend::import_incoming_files()
{
    ensure_directory(config_.music_dir);
    ensure_directory(connect_config_.cookies_incoming_dir);

    std::error_code ec;
    if (!fs::exists(connect_config_.cookies_incoming_dir, ec)) {
        return;
    }

    for (const auto &entry : fs::directory_iterator(connect_config_.cookies_incoming_dir, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        const std::string path = entry.path().string();
        if (!is_audio_extension(lower_ext(path))) {
            continue;
        }
        const std::string dest = config_.music_dir + "/incoming/" + entry.path().filename().string();
        ensure_directory(config_.music_dir + "/incoming");
        atomic_move_file(path, dest);
    }
}

void MusicBackend::ingest_file(const std::string &path)
{
    if (!is_audio_extension(lower_ext(path))) {
        return;
    }

    MusicTrack track;
    track.path = path;
    track.title = basename_no_ext(path);

    const std::string parent = parent_name(path);
    const std::string grandparent = grandparent_name(path);
    if (!grandparent.empty() && grandparent != "music" && grandparent != "incoming" &&
        grandparent != "braillatron") {
        track.artist = grandparent;
        track.album = parent.empty() ? "Unknown Album" : parent;
    } else if (!parent.empty() && parent != "music" && parent != "incoming") {
        track.artist = "Unknown Artist";
        track.album = parent;
    } else {
        track.artist = "Unknown Artist";
        track.album = "Unknown Album";
    }

    track.id = std::to_string(flat_tracks_.size());
    flat_tracks_.push_back(std::move(track));
}

void MusicBackend::build_tree()
{
    artists_.clear();
    for (const MusicTrack &track : flat_tracks_) {
        MusicArtist *artist = nullptr;
        for (MusicArtist &existing : artists_) {
            if (existing.name == track.artist) {
                artist = &existing;
                break;
            }
        }
        if (artist == nullptr) {
            artists_.push_back(MusicArtist {track.artist, {}});
            artist = &artists_.back();
        }

        MusicAlbum *album = nullptr;
        for (MusicAlbum &existing : artist->albums) {
            if (existing.name == track.album) {
                album = &existing;
                break;
            }
        }
        if (album == nullptr) {
            artist->albums.push_back(MusicAlbum {track.album, {}});
            album = &artist->albums.back();
        }
        album->tracks.push_back(track);
    }

    auto by_name = [](const auto &a, const auto &b) { return a.name < b.name; };
    std::sort(artists_.begin(), artists_.end(), by_name);
    for (MusicArtist &artist : artists_) {
        std::sort(artist.albums.begin(), artist.albums.end(), by_name);
        for (MusicAlbum &album : artist.albums) {
            std::sort(album.tracks.begin(), album.tracks.end(),
                      [](const MusicTrack &a, const MusicTrack &b) { return a.title < b.title; });
        }
    }
}

std::string MusicBackend::scan()
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"music disabled\"}";
    }

    import_incoming_files();
    flat_tracks_.clear();
    artists_.clear();

    ensure_directory(config_.music_dir);
    std::error_code ec;
    for (const auto &entry : fs::recursive_directory_iterator(config_.music_dir, ec)) {
        if (ec || !entry.is_regular_file()) {
            continue;
        }
        ingest_file(entry.path().string());
    }

    build_tree();
    load_state();

    std::ostringstream out;
    out << "{\"ok\":true,\"track_count\":" << flat_tracks_.size() << ",\"artists\":[";
    bool first_artist = true;
    for (const MusicArtist &artist : artists_) {
        if (!first_artist) {
            out << ',';
        }
        first_artist = false;
        out << "{\"name\":\"" << json_escape(artist.name) << "\",\"albums\":[";
        bool first_album = true;
        for (const MusicAlbum &album : artist.albums) {
            if (!first_album) {
                out << ',';
            }
            first_album = false;
            out << "{\"name\":\"" << json_escape(album.name) << "\",\"tracks\":[";
            bool first_track = true;
            for (const MusicTrack &track : album.tracks) {
                if (!first_track) {
                    out << ',';
                }
                first_track = false;
                out << "{\"id\":\"" << json_escape(track.id) << "\",\"title\":\""
                    << json_escape(track.title) << "\",\"path\":\"" << json_escape(track.path)
                    << "\"}";
            }
            out << "]}";
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}

const MusicTrack *MusicBackend::find_track(const std::string &track_id) const
{
    for (const MusicTrack &track : flat_tracks_) {
        if (track.id == track_id) {
            return &track;
        }
    }
    return nullptr;
}

int MusicBackend::track_index(const std::string &track_id) const
{
    for (size_t i = 0; i < flat_tracks_.size(); ++i) {
        if (flat_tracks_[i].id == track_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string MusicBackend::track_event_json(const MusicTrack &track) const
{
    return "{\"id\":\"" + json_escape(track.id) + "\",\"title\":\"" + json_escape(track.title) +
           "\",\"artist\":\"" + json_escape(track.artist) + "\",\"album\":\"" +
           json_escape(track.album) + "\",\"path\":\"" + json_escape(track.path) + "\"}";
}

void MusicBackend::save_state() const
{
    if (current_index_ < 0 || current_index_ >= static_cast<int>(flat_tracks_.size())) {
        return;
    }
    ensure_directory(config_.music_dir);
    const MusicTrack &track = flat_tracks_[static_cast<size_t>(current_index_)];
    std::ofstream out(config_.state_path);
    if (!out.is_open()) {
        return;
    }
    out << "{\n"
        << "  \"track_id\": \"" << json_escape(track.id) << "\",\n"
        << "  \"track_path\": \"" << json_escape(track.path) << "\",\n"
        << "  \"title\": \"" << json_escape(track.title) << "\",\n"
        << "  \"artist\": \"" << json_escape(track.artist) << "\",\n"
        << "  \"album\": \"" << json_escape(track.album) << "\",\n"
        << "  \"position_sec\": 0\n"
        << "}\n";
}

void MusicBackend::load_state()
{
    if (!file_exists(config_.state_path)) {
        return;
    }
    std::ifstream in(config_.state_path);
    if (!in.is_open()) {
        return;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string track_id = json_get_string(buffer.str(), "track_id");
    if (track_id.empty()) {
        return;
    }
    current_index_ = track_index(track_id);
}

std::string MusicBackend::play_index(int index)
{
    if (!config_.enabled) {
        return "{\"ok\":false,\"error\":\"music disabled\"}";
    }
    if (index < 0 || index >= static_cast<int>(flat_tracks_.size())) {
        return "{\"ok\":false,\"error\":\"invalid track\"}";
    }
    if (mpv_ == nullptr) {
        return "{\"ok\":false,\"error\":\"mpv unavailable\"}";
    }
    if (!mpv_->ensure_started()) {
        return "{\"ok\":false,\"error\":\"mpv start failed\"}";
    }

    current_index_ = index;
    paused_ = false;
    mpv_->mark_playing();
    const MusicTrack &track = flat_tracks_[static_cast<size_t>(index)];
    if (!mpv_->ipc().load_url(track.path)) {
        return "{\"ok\":false,\"error\":\"mpv load failed\"}";
    }

    save_state();
    if (events_ != nullptr) {
        events_->emit("music.playing", track_event_json(track));
    }
    return "{\"ok\":true,\"track\":" + track_event_json(track) + "}";
}

std::string MusicBackend::play(const std::string &track_id)
{
    return play_index(track_index(track_id));
}

std::string MusicBackend::pause_toggle()
{
    if (mpv_ == nullptr) {
        return "{\"ok\":false,\"error\":\"mpv unavailable\"}";
    }
    mpv_->pause_toggle();
    paused_ = mpv_->is_paused();
    if (!paused_ && events_ != nullptr && current_index_ >= 0) {
        events_->emit("music.playing",
                       track_event_json(flat_tracks_[static_cast<size_t>(current_index_)]));
    }
    return "{\"ok\":true,\"paused\":" + std::string(paused_ ? "true" : "false") + "}";
}

std::string MusicBackend::stop()
{
    if (mpv_ != nullptr) {
        mpv_->ipc().stop();
    }
    paused_ = false;
    current_index_ = -1;
    if (events_ != nullptr) {
        events_->emit("music.ended", "{}");
    }
    return "{\"ok\":true}";
}

std::string MusicBackend::next()
{
    if (flat_tracks_.empty()) {
        return "{\"ok\":false,\"error\":\"no tracks\"}";
    }
    if (current_index_ < 0) {
        return play_index(0);
    }
    const int next_index = (current_index_ + 1) % static_cast<int>(flat_tracks_.size());
    return play_index(next_index);
}

std::string MusicBackend::prev()
{
    if (flat_tracks_.empty()) {
        return "{\"ok\":false,\"error\":\"no tracks\"}";
    }
    if (current_index_ < 0) {
        return play_index(static_cast<int>(flat_tracks_.size()) - 1);
    }
    int prev_index = current_index_ - 1;
    if (prev_index < 0) {
        prev_index = static_cast<int>(flat_tracks_.size()) - 1;
    }
    return play_index(prev_index);
}

std::string MusicBackend::seek(double seconds)
{
    if (mpv_ == nullptr || current_index_ < 0) {
        return "{\"ok\":false,\"error\":\"nothing playing\"}";
    }
    if (!mpv_->ipc().seek_seconds(seconds)) {
        return "{\"ok\":false,\"error\":\"seek failed\"}";
    }
    return "{\"ok\":true,\"position_sec\":" + std::to_string(seconds) + "}";
}

std::string MusicBackend::status() const
{
    if (current_index_ < 0 || current_index_ >= static_cast<int>(flat_tracks_.size())) {
        return "{\"ok\":true,\"playing\":false}";
    }
    const MusicTrack &track = flat_tracks_[static_cast<size_t>(current_index_)];
    return "{\"ok\":true,\"playing\":" + std::string(paused_ ? "false" : "true") +
           ",\"paused\":" + std::string(paused_ ? "true" : "false") +
           ",\"track\":" + track_event_json(track) + "}";
}

} // namespace braillatron::connect
