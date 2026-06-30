#pragma once

#include "drv2605l.h"
#include "limit_sensors.h"
#include "ltc2944.h"
#include "ram_text_persistence.h"
#include "telemetry_config.h"

extern "C" {
#include "protocol.h"
}

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

namespace braillatron::telemetry {

struct TelemetrySnapshot {
    uint8_t battery_percent = BRAILLATRON_TELEMETRY_UNKNOWN;
    int8_t temperature_c = BRAILLATRON_TELEMETRY_UNKNOWN_S8;
    uint8_t limit_status = 0;
    uint16_t battery_mv = 0;
    bool motion_blocked = false;
    bool charging = false;
};

using TelemetryCallback = std::function<void(const TelemetrySnapshot &)>;

class TelemetrySentinel {
public:
    explicit TelemetrySentinel(TelemetryConfig config);
    ~TelemetrySentinel();

    TelemetrySentinel(const TelemetrySentinel &) = delete;
    TelemetrySentinel &operator=(const TelemetrySentinel &) = delete;

    void set_moonraker_client(motion::MoonrakerClient *client);

    void start(TelemetryCallback callback = nullptr);
    void stop();
    void poll_once();

    TelemetrySnapshot latest_snapshot() const;
    bool shutdown_in_progress() const;

private:
    void run_loop();
    void handle_battery_critical(uint8_t soc_percent);

    TelemetryConfig config_;
    Ltc2944 fuel_gauge_;
    LimitSensors limit_sensors_;
    RamTextPersistence persistence_;
    Drv2605l haptic_;

    std::thread worker_;
    std::atomic<bool> running_ {false};
    std::atomic<bool> shutdown_in_progress_ {false};
    TelemetryCallback callback_;
    mutable TelemetrySnapshot snapshot_;
    uint16_t last_battery_mv_ = 0;
    uint8_t charging_rise_polls_ = 0;
};

} // namespace braillatron::telemetry
