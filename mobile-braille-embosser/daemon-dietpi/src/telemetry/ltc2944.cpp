#include "ltc2944.h"

extern "C" {
#include "protocol.h"
}

#include <algorithm>
#include <cmath>

namespace braillatron::telemetry {

namespace {

constexpr uint8_t REG_VOLT_MSB = 0x06u;
constexpr uint8_t REG_CHARGE_MSB = 0x04u;
constexpr uint8_t REG_TEMP_MSB = 0x08u;

} // namespace

Ltc2944::Ltc2944(TelemetryConfig config)
    : config_(std::move(config))
    , bus_(config_.i2c_bus, config_.ltc2944_address)
{
}

bool Ltc2944::bus_available() const
{
    return bus_.is_open();
}

FuelGaugeReading Ltc2944::read()
{
    FuelGaugeReading reading {};

    if (!bus_.is_open()) {
        return reading;
    }

    uint16_t raw_voltage = 0;
    uint16_t raw_temp = 0;
    uint16_t raw_charge = 0;

    if (!bus_.read_register16(REG_VOLT_MSB, raw_voltage) ||
        !bus_.read_register16(REG_TEMP_MSB, raw_temp) ||
        !bus_.read_register16(REG_CHARGE_MSB, raw_charge)) {
        return reading;
    }

    // LTC2944 voltage full scale is 70.8 V over 16 bits (~1.0803 mV/LSB).
    const double scaled_mv = raw_voltage * config_.ltc2944_mv_per_lsb;
    if (scaled_mv < 0.0 || scaled_mv > 65535.0) {
        reading.valid = true;
        return reading;
    }
    reading.battery_mv = static_cast<uint16_t>(scaled_mv);
    reading.charge_counts = raw_charge;
    // Temperature full scale is 510 K over 16 bits (datasheet Table 3).
    reading.temperature_c =
        static_cast<int8_t>(std::lround((raw_temp * 510.0 / 65535.0) - 273.15));
    reading.valid = true;

    const bool voltage_ok = ltc2944_voltage_scale_trusted(config_) &&
                            reading.battery_mv >= 8000u && reading.battery_mv <= 20000u;
    const bool charge_mode =
        config_.battery_full_charge_counts > config_.battery_empty_charge_counts;

    if (charge_mode) {
        const uint8_t charge_soc = soc_from_charge_counts(raw_charge);
        const uint8_t volt_soc = soc_from_voltage_mv(reading.battery_mv);
        // A zeroed / unprogrammed charge register must not look like an empty
        // pack if pack voltage is still in a healthy 4S window.
        if (charge_soc == 0 && voltage_ok && volt_soc > config_.battery_critical_percent) {
            reading.soc_percent = volt_soc;
            reading.soc_trusted = true;
        } else if (charge_soc == 0 && !voltage_ok) {
            reading.soc_percent = 255;
            reading.soc_trusted = false;
        } else {
            reading.soc_percent = charge_soc;
            reading.soc_trusted = true;
        }
    } else if (voltage_ok) {
        reading.soc_percent = soc_from_voltage_mv(reading.battery_mv);
        reading.soc_trusted = true;
    }

    return reading;
}

uint8_t Ltc2944::soc_from_voltage_mv(uint16_t mv) const
{
    if (mv <= config_.battery_4s_min_mv) {
        return 0;
    }
    if (mv >= config_.battery_4s_max_mv) {
        return 100;
    }

    const uint32_t span = config_.battery_4s_max_mv - config_.battery_4s_min_mv;
    const uint32_t delta = mv - config_.battery_4s_min_mv;
    return static_cast<uint8_t>((delta * 100u) / span);
}

uint8_t Ltc2944::soc_from_charge_counts(uint32_t counts) const
{
    if (counts <= config_.battery_empty_charge_counts) {
        return 0;
    }
    if (counts >= config_.battery_full_charge_counts) {
        return 100;
    }

    const uint32_t span =
        config_.battery_full_charge_counts - config_.battery_empty_charge_counts;
    const uint32_t delta = counts - config_.battery_empty_charge_counts;
    return static_cast<uint8_t>((delta * 100u) / span);
}

} // namespace braillatron::telemetry
