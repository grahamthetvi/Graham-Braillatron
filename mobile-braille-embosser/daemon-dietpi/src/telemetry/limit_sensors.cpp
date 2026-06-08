#include "limit_sensors.h"

#include <fstream>

namespace braillatron::telemetry {

LimitSensors::LimitSensors(TelemetryConfig config)
    : config_(std::move(config))
{
}

GpioReading LimitSensors::read_gpio(const std::string &path) const
{
    GpioReading reading {};

    if (path.empty()) {
        reading.availability = GpioAvailability::Unconfigured;
        return reading;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        reading.availability = GpioAvailability::Unavailable;
        return reading;
    }

    int value = 0;
    file >> value;
    const bool raw = value != 0;
    reading.active = config_.limit_active_low ? !raw : raw;
    reading.availability =
        reading.active ? GpioAvailability::Active : GpioAvailability::Inactive;
    return reading;
}

LimitSensorState LimitSensors::read() const
{
    LimitSensorState state {};
    state.paper_edge = read_gpio(config_.gpio_paper_edge);
    state.y_home = read_gpio(config_.gpio_y_home);

    if (state.paper_edge.availability == GpioAvailability::Active) {
        state.paper_edge_active = true;
        state.limit_status |= BRAILLATRON_LIMIT_PAPER_EDGE;
    }

    if (state.y_home.availability == GpioAvailability::Active) {
        state.y_home_active = true;
        state.limit_status |= BRAILLATRON_LIMIT_Y_HOME;
    }

    return state;
}

} // namespace braillatron::telemetry
