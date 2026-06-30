#include "telemetry_sentinel.h"

#include "../motion/moonraker_client.h"
#include "../motion_gate.h"
#include "system_shutdown.h"
#include "telemetry_bridge.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace braillatron::telemetry {

namespace {

bool ip2368_charging_active(const std::string &path)
{
    if (path.empty()) {
        return false;
    }

    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }

    int value = 0;
    input >> value;
    return value != 0;
}

} // namespace

TelemetrySentinel::TelemetrySentinel(TelemetryConfig config)
    : config_(std::move(config))
    , fuel_gauge_(config_)
    , limit_sensors_(config_)
    , persistence_(config_)
    , haptic_(config_)
{
    if (!fuel_gauge_.bus_available()) {
        std::cerr << "[telemetry] I2C bus unavailable: " << config_.i2c_bus << "\n";
    }
}

TelemetrySentinel::~TelemetrySentinel()
{
    /* Joins the worker; without this a joinable thread would std::terminate. */
    stop();
}

void TelemetrySentinel::set_moonraker_client(motion::MoonrakerClient *client)
{
    limit_sensors_.set_moonraker_client(client);
}

void TelemetrySentinel::start(TelemetryCallback callback)
{
    if (running_.load()) {
        return;
    }

    callback_ = std::move(callback);
    running_ = true;
    worker_ = std::thread([this]() { run_loop(); });
}

void TelemetrySentinel::stop()
{
    running_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void TelemetrySentinel::poll_once()
{
    const FuelGaugeReading gauge = fuel_gauge_.read();
    const LimitSensorState limits = limit_sensors_.read();

    TelemetrySnapshot snapshot {};
    snapshot.battery_percent =
        gauge.valid ? gauge.soc_percent : BRAILLATRON_TELEMETRY_UNKNOWN;
    snapshot.temperature_c =
        gauge.valid ? gauge.temperature_c : BRAILLATRON_TELEMETRY_UNKNOWN_S8;
    snapshot.battery_mv = gauge.battery_mv;
    snapshot.limit_status = limits.limit_status;

    if (MotionGate::is_blocked()) {
        snapshot.limit_status |= BRAILLATRON_LIMIT_MOTION_BLOCKED;
        snapshot.motion_blocked = true;
    }

    if (gauge.valid && gauge.soc_percent < config_.battery_critical_percent) {
        snapshot.limit_status |= BRAILLATRON_LIMIT_BATTERY_CRITICAL;
        handle_battery_critical(gauge.soc_percent);
        snapshot.motion_blocked = true;
    }

    if (gauge.valid) {
        if (last_battery_mv_ != 0 &&
            gauge.battery_mv > last_battery_mv_ &&
            (gauge.battery_mv - last_battery_mv_) >= config_.charging_rise_mv) {
            ++charging_rise_polls_;
        } else if (gauge.battery_mv <= last_battery_mv_) {
            charging_rise_polls_ = 0;
        }

        if (charging_rise_polls_ >= config_.charging_polls_required ||
            ip2368_charging_active(config_.ip2368_status_path)) {
            snapshot.charging = true;
        }

        last_battery_mv_ = gauge.battery_mv;
    }

    snapshot_ = snapshot;
    write_telemetry_json(kTelemetryJsonPath, snapshot_);

    if (callback_) {
        callback_(snapshot_);
    }
}

TelemetrySnapshot TelemetrySentinel::latest_snapshot() const
{
    return snapshot_;
}

bool TelemetrySentinel::shutdown_in_progress() const
{
    return shutdown_in_progress_.load();
}

void TelemetrySentinel::run_loop()
{
    while (running_.load()) {
        poll_once();
        std::this_thread::sleep_for(std::chrono::milliseconds(config_.poll_interval_ms));
    }
}

void TelemetrySentinel::handle_battery_critical(uint8_t soc_percent)
{
    if (shutdown_in_progress_.exchange(true)) {
        return;
    }

    std::cerr << "[telemetry] battery critical at " << static_cast<unsigned>(soc_percent)
              << "%, initiating graceful shutdown\n";

    MotionGate::block("battery below critical threshold");

    if (!persistence_.persist_layers_transactional()) {
        std::cerr << "[telemetry] warning: RAM text persistence failed\n";
    }

    if (!config_.coordinate_ram_path.empty()) {
        std::ifstream coords_in(config_.coordinate_ram_path);
        if (coords_in.good()) {
            const std::string dest = config_.persistent_output_dir + "/coords.json";
            std::ofstream coords_out(dest + ".tmp", std::ios::trunc);
            coords_out << coords_in.rdbuf();
            coords_out.close();
            std::rename((dest + ".tmp").c_str(), dest.c_str());
        }
    }

    if (!haptic_.play_shutdown_profile()) {
        std::cerr << "[telemetry] warning: DRV2605L shutdown haptic failed\n";
    }

    if (!request_clean_shutdown()) {
        std::cerr << "[telemetry] warning: shutdown command failed\n";
    }
}

} // namespace braillatron::telemetry
