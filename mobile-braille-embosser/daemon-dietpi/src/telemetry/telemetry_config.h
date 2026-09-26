#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace braillatron::telemetry {

struct TelemetryConfig {
    std::string i2c_bus = "/dev/i2c-1";
    uint8_t ltc2944_address = 0x64;
    uint8_t drv2605l_address = 0x5A;
    uint8_t battery_low_percent = 20;
    uint8_t battery_critical_percent = 5;
    uint32_t poll_interval_ms = 500;

    std::string coordinate_ram_path = "/var/lib/braillatron/ram/coords.json";
    std::string homing_status_path = "/run/braillatron/homing.status";
    std::string sentry_dsn;
    std::string memfault_project_key;
    std::string build_version = "braillatron-dev";

    uint16_t battery_4s_min_mv = 12000;
    uint16_t battery_4s_max_mv = 16800;
    // LTC2944 datasheet Table 3: 70.8 V full-scale / 65535 counts ≈ 1.0803 mV/LSB.
    // Independent of Rsense. The old 58.6 default collapsed 4S SOC to ~0% (or
    // overflowed uint16) and made the 20%/5% shutdown policy meaningless.
    // Coulomb counting is a separate path and stays off until Rsense is known.
    double ltc2944_mv_per_lsb = 1.0803;

    // Coulomb SOC (optional). 0/0 disables charge-count mode. Requires a known
    // high-side Rsense; V5.1 does not specify one, so leave these at 0 until
    // the sense resistor is measured and full/empty counts are captured.
    uint32_t battery_full_charge_counts = 0;
    uint32_t battery_empty_charge_counts = 0;

    // Empty on V5.1: limits are Klipper X-STOP/Y-STOP via Moonraker, not Pi GPIO.
    std::string gpio_paper_edge;
    std::string gpio_y_home;
    bool limit_active_low = true;

    std::vector<std::string> ram_text_layers;
    std::string persistent_output_dir = "/var/lib/braillatron/documents";

    uint8_t shutdown_waveform_effect = 47;

    uint16_t charging_rise_mv = 50;
    uint8_t charging_polls_required = 3;
    // Unused on V5.1: no IP2368 charge-status GPIO. Charging is voltage-rise.
    std::string ip2368_status_path;
};

TelemetryConfig load_telemetry_config(const std::string &path);

/** True when ltc2944_mv_per_lsb matches the datasheet LSB (not the old 58.6). */
bool ltc2944_voltage_scale_trusted(const TelemetryConfig &config);

} // namespace braillatron::telemetry
