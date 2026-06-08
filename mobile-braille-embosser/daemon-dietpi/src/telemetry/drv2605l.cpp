#include "drv2605l.h"

namespace braillatron::telemetry {

namespace {

constexpr uint8_t REG_MODE = 0x01u;
constexpr uint8_t REG_LIBRARY = 0x03u;
constexpr uint8_t REG_WAVEFORM_SEQ1 = 0x04u;
constexpr uint8_t REG_WAVEFORM_SEQ2 = 0x05u;
constexpr uint8_t REG_GO = 0x0Eu;
constexpr uint8_t REG_LRA_OPEN_LOOP = 0x1Au;

constexpr uint8_t MODE_INTERNAL_TRIGGER = 0x00u;
constexpr uint8_t LIBRARY_LRA = 0x06u;
constexpr uint8_t GO_FIRE = 0x01u;
constexpr uint8_t END_SEQUENCE = 0x00u;

} // namespace

Drv2605l::Drv2605l(TelemetryConfig config)
    : config_(std::move(config))
    , bus_(config_.i2c_bus, config_.drv2605l_address)
{
}

bool Drv2605l::initialize_lra()
{
    if (!bus_.is_open()) {
        return false;
    }

    if (!bus_.write_register(REG_MODE, MODE_INTERNAL_TRIGGER)) {
        return false;
    }

    if (!bus_.write_register(REG_LIBRARY, LIBRARY_LRA)) {
        return false;
    }

    return bus_.write_register(REG_LRA_OPEN_LOOP, 0x86u);
}

bool Drv2605l::play_effect(uint8_t effect_id)
{
    if (!initialize_lra()) {
        return false;
    }

    if (!bus_.write_register(REG_WAVEFORM_SEQ1, effect_id)) {
        return false;
    }

    if (!bus_.write_register(REG_WAVEFORM_SEQ2, END_SEQUENCE)) {
        return false;
    }

    return bus_.write_register(REG_GO, GO_FIRE);
}

bool Drv2605l::play_shutdown_profile()
{
    return play_effect(config_.shutdown_waveform_effect);
}

} // namespace braillatron::telemetry
