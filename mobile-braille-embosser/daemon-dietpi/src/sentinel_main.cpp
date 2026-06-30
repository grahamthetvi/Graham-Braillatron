#include "hardware/hardware_config.h"
#include "motion/klipper_config.h"
#include "motion/moonraker_client.h"
#include "motion_gate.h"
#include "telemetry/crash_reporter.h"
#include "telemetry/homing_service.h"
#include "telemetry/telemetry_config.h"
#include "telemetry/telemetry_sentinel.h"

#include <cstdlib>
#include <csignal>
#include <fstream>
#include <iostream>
#include <memory>
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

int32_t read_target_y_line(const std::string &coords_path)
{
    std::ifstream input(coords_path);
    if (!input.is_open()) {
        return 0;
    }
    std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::string key = "\"y_line_index\":";
    const size_t pos = content.find(key);
    if (pos == std::string::npos) {
        return 0;
    }
    return static_cast<int32_t>(std::strtol(content.c_str() + pos + key.size(), nullptr, 10));
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

        const std::string klipper_path = resolve_config_path(base, hardware.klipper_config);
        const braillatron::motion::KlipperConfig klipper_config =
            braillatron::motion::load_klipper_config(klipper_path);

        std::unique_ptr<braillatron::motion::MoonrakerClient> moonraker;
        braillatron::motion::MoonrakerClient *moonraker_ptr = nullptr;
        if (hardware.motion_enabled && klipper_config.enabled) {
            moonraker = std::make_unique<braillatron::motion::MoonrakerClient>(klipper_config);
            if (moonraker->ping()) {
                moonraker_ptr = moonraker.get();
                std::cerr << "telemetry: Klipper/Moonraker connected for limits and homing\n";
            }
        }

        braillatron::telemetry::CrashReporterConfig crash_config;
        crash_config.sentry_dsn = config.sentry_dsn;
        crash_config.memfault_project_key = config.memfault_project_key;
        crash_config.build_version = config.build_version;
        braillatron::telemetry::install_crash_reporter(crash_config);

        braillatron::telemetry::HomingService homing(config);
        homing.set_moonraker_client(moonraker_ptr);
        if (hardware.motion_enabled) {
            const int32_t target_y = read_target_y_line(config.coordinate_ram_path);
            homing.run_boot_homing(target_y);
            homing.write_status(config.homing_status_path);
        }

        braillatron::telemetry::TelemetrySentinel sentinel(config);
        sentinel.set_moonraker_client(moonraker_ptr);

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
