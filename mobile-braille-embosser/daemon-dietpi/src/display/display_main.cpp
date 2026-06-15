#include "display_service.h"
#include "remote_display_config.h"

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

    const std::string config_path = braillatron::display::remote_display_config_path();
    auto config = braillatron::display::load_remote_display_config(config_path);
    braillatron::display::DisplayService service(config);
    service.start();

    std::cerr << "[displayd] started (config=" << config_path << ", enabled="
              << (config.enabled ? "true" : "false") << ")\n";

    while (g_running) {
        service.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    service.stop();
    return 0;
}
