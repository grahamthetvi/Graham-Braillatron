#include "connect_service.h"

#include "connect_config.h"

#include <csignal>
#include <iostream>
#include <thread>

namespace {

volatile std::sig_atomic_t g_running = 1;

void handle_signal(int)
{
    g_running = 0;
}

} // namespace

int main()
{
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const std::string base = braillatron::connect::config_dir_from_env();
    const auto connect_config = braillatron::connect::load_connect_config(
        braillatron::connect::resolve_config_path(base, "connect.conf"));
    const auto youtube_config = braillatron::connect::load_youtube_config(
        braillatron::connect::resolve_config_path(base, "youtube.conf"));
    const auto messages_config = braillatron::connect::load_messages_config(
        braillatron::connect::resolve_config_path(base, "messages.conf"));
    const auto music_config = braillatron::connect::load_music_config(
        braillatron::connect::resolve_config_path(base, "music.conf"));
    auto weather_config = braillatron::connect::load_weather_config(
        braillatron::connect::resolve_config_path(base, "weather.conf"));
    weather_config.config_path =
        braillatron::connect::resolve_config_path(base, "weather.conf");
    const auto podcasts_config = braillatron::connect::load_podcasts_config(
        braillatron::connect::resolve_config_path(base, "podcasts.conf"));
    const auto radio_config = braillatron::connect::load_radio_config(
        braillatron::connect::resolve_config_path(base, "radio.conf"));
    const auto library_config = braillatron::connect::load_library_config(
        braillatron::connect::resolve_config_path(base, "library.conf"));
    const auto worthwhile_config = braillatron::connect::load_worthwhile_config(
        braillatron::connect::resolve_config_path(base, "worthwhile.conf"));
    const auto gmail_config = braillatron::connect::load_gmail_config(
        braillatron::connect::resolve_config_path(base, "gmail.conf"));

    braillatron::connect::ConnectService service(connect_config, youtube_config, messages_config,
                                                 music_config, weather_config, podcasts_config,
                                                 radio_config, library_config, worthwhile_config,
                                                 gmail_config);
    service.start();

    while (g_running) {
        service.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    service.stop();
    return 0;
}
