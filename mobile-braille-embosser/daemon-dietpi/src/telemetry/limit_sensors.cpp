#include "limit_sensors.h"

#include "../motion/moonraker_client.h"

#include <fstream>

namespace braillatron::telemetry {

LimitSensors::LimitSensors(TelemetryConfig config)
    : config_(std::move(config))
{
}

void LimitSensors::set_moonraker_client(motion::MoonrakerClient *client)
{
    moonraker_ = client;
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

LimitSensorState LimitSensors::read_klipper_endstops() const
{
    LimitSensorState state {};
    if (moonraker_ == nullptr) {
        return state;
    }

    const motion::EndstopState endstops = moonraker_->query_endstops();
    if (!endstops.query_ok) {
        return state;
    }

    if (endstops.paper_edge) {
        state.paper_edge_active = true;
        state.paper_edge.availability = GpioAvailability::Active;
        state.limit_status |= BRAILLATRON_LIMIT_PAPER_EDGE;
    } else {
        state.paper_edge.availability = GpioAvailability::Inactive;
    }

    if (endstops.y_home) {
        state.y_home_active = true;
        state.y_home.availability = GpioAvailability::Active;
        state.limit_status |= BRAILLATRON_LIMIT_Y_HOME;
    } else {
        state.y_home.availability = GpioAvailability::Inactive;
    }

    return state;
}

LimitSensorState LimitSensors::read() const
{
    if (config_.gpio_paper_edge.empty() && config_.gpio_y_home.empty() && moonraker_ != nullptr) {
        return read_klipper_endstops();
    }

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
