#pragma once

#include "telemetry_config.h"

extern "C" {
#include "protocol.h"
}

#include <cstdint>
#include <string>

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

    LimitSensorState read() const;

private:
    GpioReading read_gpio(const std::string &path) const;

    TelemetryConfig config_;
};

} // namespace braillatron::telemetry
