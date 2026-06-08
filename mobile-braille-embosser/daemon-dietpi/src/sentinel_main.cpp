#include "hardware/hardware_config.h"
#include "motion_gate.h"
#include "telemetry/telemetry_config.h"
#include "telemetry/telemetry_sentinel.h"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t g_running = 1;

void handle_signal(int)
{
    g_running = 0;
}

std::string config_dir()
{
    const char *env = std::getenv("BRAILLATRON_CONFIG");
    if (env != nullptr && env[0] != '\0') {
        return env;
    }
    return "config";
}

std::string resolve_config_path(const std::string &base, const std::string &path)
{
    if (path.empty() || path[0] == '/') {
        return path;
    }
    return base + "/" + path;
}

} // namespace

int main(int argc, char *argv[])
{
    const std::string base = config_dir();
    const std::string hardware_path =
        argc > 1 ? argv[1] : resolve_config_path(base, "hardware.conf");

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    try {
        const braillatron::hardware::HardwareConfig hardware =
            braillatron::hardware::load_hardware_config(hardware_path);

        if (!hardware.motion_enabled) {
            braillatron::MotionGate::block("motion_disabled_in_config");
            std::cerr << "telemetry: motion disabled (motion_enabled=false in hardware.conf)\n";
        }

        const std::string telemetry_path =
            argc > 2 ? argv[2] : resolve_config_path(base, hardware.telemetry_config);

        braillatron::telemetry::TelemetryConfig config =
            braillatron::telemetry::load_telemetry_config(telemetry_path);
        braillatron::telemetry::TelemetrySentinel sentinel(config);

        sentinel.start([](const braillatron::telemetry::TelemetrySnapshot &snapshot) {
            std::cerr << "[telemetry] soc=" << static_cast<unsigned>(snapshot.battery_percent)
                      << "% temp=" << static_cast<int>(snapshot.temperature_c)
                      << "C limits=0x" << std::hex << static_cast<unsigned>(snapshot.limit_status)
                      << std::dec << "\n";
        });

        while (g_running && !sentinel.shutdown_in_progress()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    } catch (const std::exception &ex) {
        std::cerr << "telemetry sentinel error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
