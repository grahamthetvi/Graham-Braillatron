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

    braillatron::connect::ConnectService service(connect_config, youtube_config, messages_config);
    service.start();

    while (g_running) {
        service.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    service.stop();
    return 0;
}
