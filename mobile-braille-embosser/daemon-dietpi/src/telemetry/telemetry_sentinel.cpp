#include "telemetry_sentinel.h"

#include "../motion_gate.h"
#include "system_shutdown.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace braillatron::telemetry {

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

    snapshot_ = snapshot;

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

    if (!haptic_.play_shutdown_profile()) {
        std::cerr << "[telemetry] warning: DRV2605L shutdown haptic failed\n";
    }

    if (!request_clean_shutdown()) {
        std::cerr << "[telemetry] warning: shutdown command failed\n";
    }
}

} // namespace braillatron::telemetry
