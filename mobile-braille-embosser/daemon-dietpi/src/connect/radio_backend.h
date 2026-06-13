#pragma once

#include "connect_config.h"
#include "event_writer.h"
#include "mpv_service.h"

#include <cstdint>
#include <string>
#include <vector>

namespace braillatron::connect {

struct RadioStation {
    std::string id;
    std::string name;
    std::string url;
    std::string country;
    std::string tags;
    bool favorite = false;
};

class RadioBackend {
public:
    RadioBackend(RadioConfig config, MpvService *mpv, EventWriter *events);

    std::string list_stations();
    std::string search(const std::string &query);
    std::string play(const std::string &station_id);
    std::string pause_toggle();
    std::string stop();
    std::string status() const;
    std::string favorites_add(const std::string &station_id);
    std::string favorites_remove(const std::string &station_id);
    std::string favorites_list();

    void poll_metadata();

    static std::string parse_icy_title(const std::string &headers);
    static std::vector<RadioStation> parse_stations_json(const std::string &json);

private:
    bool load_bundled();
    bool load_favorites();
    bool save_favorites() const;
    std::string curl_fetch(const std::string &url) const;
    std::string curl_headers(const std::string &url) const;
    const RadioStation *find_station(const std::string &station_id) const;
    RadioStation *find_station_mut(const std::string &station_id);
    std::string station_event_json(const RadioStation &station) const;
    std::string merge_station_lists() const;

    RadioConfig config_;
    MpvService *mpv_;
    EventWriter *events_;
    std::vector<RadioStation> bundled_;
    std::vector<RadioStation> favorites_;
    std::vector<RadioStation> search_results_;
    std::string current_station_id_;
    std::string last_metadata_;
    bool paused_ = false;
};

} // namespace braillatron::connect
