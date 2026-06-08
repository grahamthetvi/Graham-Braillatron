#pragma once

#include "i2c_device.h"
#include "telemetry_config.h"

namespace braillatron::telemetry {

class Drv2605l {
public:
    explicit Drv2605l(TelemetryConfig config);

    bool play_shutdown_profile();
    bool play_effect(uint8_t effect_id);

private:
    TelemetryConfig config_;
    I2cDevice bus_;

    bool initialize_lra();
};

} // namespace braillatron::telemetry
