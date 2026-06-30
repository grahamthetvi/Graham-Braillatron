#pragma once

#include "telemetry_config.h"

extern "C" {
#include "protocol.h"
}

#include <cstdint>
#include <string>

namespace braillatron::motion {
class MoonrakerClient;
}

namespace braillatron::telemetry {

enum class GpioAvailability {
    Unconfigured,
    Unavailable,
    Active,
    Inactive,
};

struct GpioReading {
    GpioAvailability availability = GpioAvailability::Unconfigured;
    bool active = false;
};

struct LimitSensorState {
    GpioReading paper_edge;
    GpioReading y_home;
    bool paper_edge_active = false;
    bool y_home_active = false;
    uint8_t limit_status = 0;
};

class LimitSensors {
public:
    explicit LimitSensors(TelemetryConfig config);

    void set_moonraker_client(motion::MoonrakerClient *client);

    LimitSensorState read() const;

private:
    GpioReading read_gpio(const std::string &path) const;
    LimitSensorState read_klipper_endstops() const;

    TelemetryConfig config_;
    motion::MoonrakerClient *moonraker_ = nullptr;
};

} // namespace braillatron::telemetry
