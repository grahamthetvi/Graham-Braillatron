#pragma once

#include "connect_config.h"
#include "event_writer.h"
#include "mpv_service.h"

#include <string>
#include <vector>

namespace braillatron::connect {

struct MusicTrack {
    std::string id;
    std::string title;
    std::string path;
    std::string artist;
    std::string album;
};

struct MusicAlbum {
    std::string name;
    std::vector<MusicTrack> tracks;
};

struct MusicArtist {
    std::string name;
    std::vector<MusicAlbum> albums;
};

class MusicBackend {
public:
    MusicBackend(MusicConfig config, ConnectConfig connect_config, MpvService *mpv,
                 EventWriter *events);

    std::string scan();
    std::string play(const std::string &track_id);
    std::string pause_toggle();
    std::string stop();
    std::string next();
    std::string prev();
    std::string seek(double seconds);
    std::string status() const;

    const std::vector<MusicTrack> &tracks() const { return flat_tracks_; }

private:
    bool is_audio_extension(const std::string &ext) const;
    void ingest_file(const std::string &path);
    void import_incoming_files();
    void build_tree();
    void save_state() const;
    void load_state();
    const MusicTrack *find_track(const std::string &track_id) const;
    int track_index(const std::string &track_id) const;
    std::string play_index(int index);
    std::string track_event_json(const MusicTrack &track) const;

    MusicConfig config_;
    ConnectConfig connect_config_;
    MpvService *mpv_;
    EventWriter *events_;
    std::vector<MusicTrack> flat_tracks_;
    std::vector<MusicArtist> artists_;
    int current_index_ = -1;
    bool paused_ = false;
};

} // namespace braillatron::connect
