#include "connect_service.h"

#include "connect_async.h"
#include "json_utils.h"
#include "subprocess.h"

#include <chrono>
#include <iostream>

namespace braillatron::connect {

namespace {

uint64_t now_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

uint64_t unix_now_sec()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

} // namespace

ConnectService::ConnectService(ConnectConfig connect_config, YoutubeConfig youtube_config,
                               MessagesConfig messages_config, MusicConfig music_config,
                               WeatherConfig weather_config, PodcastsConfig podcasts_config,
                               RadioConfig radio_config, LibraryConfig library_config,
                               GmailConfig gmail_config)
    : connect_config_(std::move(connect_config))
    , events_(connect_config_.event_path)
    , mpv_(MpvService::Options {
          youtube_config.mpv_path,
          youtube_config.mpv_ao,
          connect_config_.mpv_socket_path,
          std::string {},
      })
    , youtube_(std::move(youtube_config), connect_config_, &mpv_, &events_)
    , music_(std::move(music_config), connect_config_, &mpv_, &events_)
    , weather_(std::move(weather_config), &events_)
    , podcasts_(std::move(podcasts_config), connect_config_, &mpv_, &events_)
    , radio_(std::move(radio_config), &mpv_, &events_)
    , library_(std::move(library_config))
    , gmail_(std::move(gmail_config), &events_)
    , signal_(std::move(messages_config), &events_)
    , server_(connect_config_.socket_path)
{
}

void ConnectService::start()
{
    ensure_directory(connect_config_.credentials_dir);
    ensure_directory(connect_config_.cookies_incoming_dir);
    ensure_directory(connect_config_.credentials_dir + "/signal-cli");
    ensure_directory(connect_config_.credentials_dir + "/gmail");
    run_command("chmod 700 " + connect_config_.credentials_dir + "/gmail");

    jobs_.start(&events_);
    signal_.start_daemon_if_linked();
    signal_.start_event_thread();

    if (!server_.listen()) {
        std::cerr << "[connectd] failed to listen on " << connect_config_.socket_path << "\n";
        return;
    }
    running_ = true;
    std::cerr << "[connectd] listening on " << connect_config_.socket_path << "\n";
}

void ConnectService::stop()
{
    running_ = false;
    jobs_.stop();
    signal_.stop_event_thread();
    signal_.stop_daemon();
    mpv_.stop();
    server_.close();
}

void ConnectService::poll()
{
    if (!running_.load()) {
        return;
    }

    const uint64_t now = now_ms();
    if (now - last_cookie_poll_ms_ >= connect_config_.cookie_poll_ms) {
        youtube_.poll_cookie_import();
        last_cookie_poll_ms_ = now;
    }

    const uint64_t now_sec = unix_now_sec();
    if (last_podcast_refresh_sec_ == 0 ||
        now_sec - last_podcast_refresh_sec_ >= 300) {
        podcasts_.poll_refresh(now_sec);
        last_podcast_refresh_sec_ = now_sec;
    }

    weather_.poll_refresh(now_sec);

    if (last_radio_metadata_poll_ms_ == 0 ||
        now - last_radio_metadata_poll_ms_ >= 30000) {
        radio_.poll_metadata();
        last_radio_metadata_poll_ms_ = now;
    }

    server_.poll_once([this](const std::string &request) { return handle_request(request); });
}

std::string ConnectService::cmd_from_request(const std::string &request) const
{
    return json_get_string(request, "cmd");
}

std::string ConnectService::media_pause_toggle()
{
    if (mpv_.ensure_started()) {
        mpv_.pause_toggle();
        return "{\"ok\":true,\"paused\":" + std::string(mpv_.is_paused() ? "true" : "false") + "}";
    }
    return "{\"ok\":false,\"error\":\"mpv unavailable\"}";
}

std::string ConnectService::media_set_pause(bool pause)
{
    if (!mpv_.ensure_started()) {
        return "{\"ok\":false,\"error\":\"mpv unavailable\"}";
    }
    if (!mpv_.set_paused(pause)) {
        return "{\"ok\":false,\"error\":\"mpv pause failed\"}";
    }
    return "{\"ok\":true,\"paused\":" + std::string(pause ? "true" : "false") + "}";
}

std::string ConnectService::execute_command(const std::string &cmd, const std::string &request)
{
    if (cmd == "ping") {
        return "{\"ok\":true,\"service\":\"connectd\"}";
    }
    if (cmd == "accounts.status") {
        const bool youtube_cookies = youtube_.cookies_present();
        return "{\"ok\":true,\"youtube_cookies\":" +
               std::string(youtube_cookies ? "true" : "false") + ",\"signal_linked\":" +
               std::string(signal_.is_linked() ? "true" : "false") + ",\"gmail_linked\":" +
               std::string(gmail_.is_linked() ? "true" : "false") + "}";
    }
    if (cmd == "media.pause") {
        return media_pause_toggle();
    }
    if (cmd == "media.set_pause") {
        return media_set_pause(json_get_bool(request, "pause", true));
    }
    if (cmd == "youtube.search") {
        return youtube_.search(json_get_string(request, "query"));
    }
    if (cmd == "youtube.play") {
        return youtube_.play(json_get_string(request, "url"));
    }
    if (cmd == "youtube.pause") {
        return youtube_.pause_toggle();
    }
    if (cmd == "youtube.stop") {
        return youtube_.stop();
    }
    if (cmd == "youtube.import_cookies") {
        youtube_.poll_cookie_import();
        return "{\"ok\":true,\"cookies\":" +
               std::string(youtube_.cookies_present() ? "true" : "false") + "}";
    }
    if (cmd == "music.scan") {
        return music_.scan();
    }
    if (cmd == "music.play") {
        return music_.play(json_get_string(request, "track_id"));
    }
    if (cmd == "music.pause") {
        return music_.pause_toggle();
    }
    if (cmd == "music.stop") {
        return music_.stop();
    }
    if (cmd == "music.next") {
        return music_.next();
    }
    if (cmd == "music.prev") {
        return music_.prev();
    }
    if (cmd == "music.seek") {
        const std::string seconds = json_get_string(request, "seconds");
        return music_.seek(seconds.empty() ? 0.0 : std::stod(seconds));
    }
    if (cmd == "music.status") {
        return music_.status();
    }
    if (cmd == "weather.fetch") {
        return weather_.fetch();
    }
    if (cmd == "weather.read") {
        return weather_.read_cache();
    }
    if (cmd == "weather.list") {
        return weather_.list_cities();
    }
    if (cmd == "weather.select") {
        const std::string slot = json_get_string(request, "slot");
        return weather_.select_city(slot.empty() ? 0 : static_cast<size_t>(std::stoul(slot)));
    }
    if (cmd == "weather.set_city") {
        const std::string slot = json_get_string(request, "slot");
        return weather_.set_city(slot.empty() ? 0 : static_cast<size_t>(std::stoul(slot)),
                                 json_get_string(request, "city_name"));
    }
    if (cmd == "weather.status") {
        return weather_.status();
    }
    if (cmd == "weather.set_location") {
        return weather_.set_location(json_get_string(request, "city_name"));
    }
    if (cmd == "weather.set_temperature_unit") {
        return weather_.set_temperature_unit(json_get_string(request, "temperature_unit"));
    }
    if (cmd == "weather.config") {
        return weather_.config_status();
    }
    if (cmd == "weather.alerts") {
        return weather_.alerts();
    }
    if (cmd == "podcasts.list_feeds") {
        return podcasts_.list_feeds();
    }
    if (cmd == "podcasts.list_episodes") {
        return podcasts_.list_episodes(json_get_string(request, "feed_id"));
    }
    if (cmd == "podcasts.play") {
        return podcasts_.play(json_get_string(request, "episode_id"));
    }
    if (cmd == "podcasts.pause") {
        return podcasts_.pause_toggle();
    }
    if (cmd == "podcasts.stop") {
        return podcasts_.stop();
    }
    if (cmd == "podcasts.status") {
        return podcasts_.status();
    }
    if (cmd == "podcasts.import_opml") {
        return podcasts_.import_opml();
    }
    if (cmd == "radio.list_stations") {
        return radio_.list_stations();
    }
    if (cmd == "radio.play") {
        return radio_.play(json_get_string(request, "station_id"));
    }
    if (cmd == "radio.pause") {
        return radio_.pause_toggle();
    }
    if (cmd == "radio.stop") {
        return radio_.stop();
    }
    if (cmd == "radio.status") {
        return radio_.status();
    }
    if (cmd == "radio.favorites.add") {
        return radio_.favorites_add(json_get_string(request, "station_id"));
    }
    if (cmd == "radio.favorites.remove") {
        return radio_.favorites_remove(json_get_string(request, "station_id"));
    }
    if (cmd == "radio.favorites.list") {
        return radio_.favorites_list();
    }
    if (cmd == "library.search") {
        return library_.search(json_get_string(request, "query"));
    }
    if (cmd == "library.download") {
        const std::string id = json_get_string(request, "gutenberg_id");
        return library_.download(id.empty() ? 0 : std::stoi(id));
    }
    if (cmd == "library.list_local") {
        return library_.list_local();
    }
    if (cmd == "library.status") {
        return library_.status();
    }
    if (cmd == "signal.start_link") {
        return signal_.run_link_workflow();
    }
    if (cmd == "signal.link_status") {
        return signal_.link_status();
    }
    if (cmd == "signal.finish_link") {
        return signal_.finish_link();
    }
    if (cmd == "signal.list_chats") {
        return signal_.list_chats();
    }
    if (cmd == "signal.list_messages") {
        return signal_.list_messages(json_get_string(request, "recipient"));
    }
    if (cmd == "signal.send") {
        return signal_.send_message(json_get_string(request, "recipient"),
                                    json_get_string(request, "text"));
    }
    if (cmd == "gmail.link_status") {
        return gmail_.link_status();
    }
    if (cmd == "gmail.start_link") {
        return gmail_.run_link_workflow();
    }
    if (cmd == "gmail.list_inbox") {
        return gmail_.list_inbox();
    }
    if (cmd == "gmail.read_message") {
        return gmail_.read_message(json_get_string(request, "message_id"));
    }
    if (cmd == "gmail.send") {
        return gmail_.send_message(json_get_string(request, "to"),
                                   json_get_string(request, "subject"),
                                   json_get_string(request, "body"));
    }
    if (cmd == "gmail.reply") {
        return gmail_.reply_message(json_get_string(request, "message_id"),
                                    json_get_string(request, "body"));
    }
    if (cmd == "gmail.archive") {
        return gmail_.archive_message(json_get_string(request, "message_id"));
    }
    if (cmd == "gmail.delete") {
        return gmail_.delete_message(json_get_string(request, "message_id"));
    }
    if (cmd == "gmail.star") {
        return gmail_.star_message(json_get_string(request, "message_id"));
    }
    if (cmd == "gmail.unlink") {
        return gmail_.unlink();
    }
    return "{\"ok\":false,\"error\":\"unknown cmd\"}";
}

std::string ConnectService::handle_request(const std::string &request)
{
    const std::string cmd = cmd_from_request(request);
    if (cmd.empty()) {
        return "{\"ok\":false,\"error\":\"missing cmd\"}";
    }

    if (!is_async_command(cmd)) {
        return execute_command(cmd, request);
    }

    const std::string request_id = request_id_from_json(request);
    SignalBackend *signal_ptr = &signal_;
    YoutubeBackend *youtube_ptr = &youtube_;
    MusicBackend *music_ptr = &music_;
    WeatherBackend *weather_ptr = &weather_;
    RssBackend *podcasts_ptr = &podcasts_;
    RadioBackend *radio_ptr = &radio_;
    LibraryBackend *library_ptr = &library_;
    GmailBackend *gmail_ptr = &gmail_;
    jobs_.submit(ConnectJob {
        request_id,
        [this, cmd, request, request_id, signal_ptr, youtube_ptr, music_ptr, weather_ptr,
         podcasts_ptr, radio_ptr, library_ptr, gmail_ptr](EventWriter *events) {
            std::string result;
            if (cmd == "signal.start_link") {
                result = signal_ptr->run_link_workflow();
            } else if (cmd == "signal.finish_link") {
                result = signal_ptr->finish_link();
            } else if (cmd == "signal.list_chats") {
                result = signal_ptr->list_chats();
            } else if (cmd == "signal.list_messages") {
                result = signal_ptr->list_messages(json_get_string(request, "recipient"));
            } else if (cmd == "signal.send") {
                result = signal_ptr->send_message(json_get_string(request, "recipient"),
                                                  json_get_string(request, "text"));
            } else if (cmd == "youtube.search") {
                result = youtube_ptr->search(json_get_string(request, "query"));
            } else if (cmd == "music.scan") {
                result = music_ptr->scan();
            } else if (cmd == "weather.fetch") {
                result = weather_ptr->fetch();
            } else if (cmd == "weather.set_location") {
                result = weather_ptr->set_location(json_get_string(request, "city_name"));
            } else if (cmd == "weather.set_city") {
                const std::string slot = json_get_string(request, "slot");
                result = weather_ptr->set_city(slot.empty() ? 0 : static_cast<size_t>(std::stoul(slot)),
                                               json_get_string(request, "city_name"));
            } else if (cmd == "weather.set_temperature_unit") {
                result = weather_ptr->set_temperature_unit(
                    json_get_string(request, "temperature_unit"));
            } else if (cmd == "podcasts.refresh") {
                result = podcasts_ptr->refresh();
            } else if (cmd == "podcasts.download") {
                result = podcasts_ptr->download(json_get_string(request, "episode_id"));
            } else if (cmd == "radio.search") {
                result = radio_ptr->search(json_get_string(request, "query"));
            } else if (cmd == "library.search") {
                result = library_ptr->search(json_get_string(request, "query"));
            } else if (cmd == "library.download") {
                const std::string id = json_get_string(request, "gutenberg_id");
                result = library_ptr->download(id.empty() ? 0 : std::stoi(id));
            } else if (cmd == "gmail.start_link") {
                result = gmail_ptr->run_link_workflow();
            } else if (cmd == "gmail.list_inbox") {
                result = gmail_ptr->list_inbox();
            } else if (cmd == "gmail.read_message") {
                result = gmail_ptr->read_message(json_get_string(request, "message_id"));
            } else if (cmd == "gmail.send") {
                result = gmail_ptr->send_message(json_get_string(request, "to"),
                                                 json_get_string(request, "subject"),
                                                 json_get_string(request, "body"));
            } else if (cmd == "gmail.reply") {
                result = gmail_ptr->reply_message(json_get_string(request, "message_id"),
                                                  json_get_string(request, "body"));
            } else if (cmd == "gmail.archive") {
                result = gmail_ptr->archive_message(json_get_string(request, "message_id"));
            } else if (cmd == "gmail.delete") {
                result = gmail_ptr->delete_message(json_get_string(request, "message_id"));
            } else if (cmd == "gmail.star") {
                result = gmail_ptr->star_message(json_get_string(request, "message_id"));
            } else {
                result = execute_command(cmd, request);
            }
            emit_connect_response(events, request_id, result);
        },
    });
    return make_pending_response(request_id);
}

} // namespace braillatron::connect
