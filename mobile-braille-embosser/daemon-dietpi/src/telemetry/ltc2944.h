#pragma once

#include "i2c_device.h"
#include "telemetry_config.h"

#include <cstdint>
#include <optional>

namespace braillatron::telemetry {

struct FuelGaugeReading {
    uint8_t soc_percent = 255;
    int8_t temperature_c = 127;
    uint16_t battery_mv = 0;
    uint32_t charge_counts = 0;
    bool valid = false;
};

class Ltc2944 {
public:
    explicit Ltc2944(TelemetryConfig config);

    FuelGaugeReading read();
    bool bus_available() const;

private:
    TelemetryConfig config_;
    I2cDevice bus_;

    uint8_t soc_from_voltage_mv(uint16_t mv) const;
    uint8_t soc_from_charge_counts(uint32_t counts) const;
};

} // namespace braillatron::telemetry
