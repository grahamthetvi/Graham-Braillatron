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
    double ltc2944_mv_per_lsb = 58.6;

    uint32_t battery_full_charge_counts = 0;
    uint32_t battery_empty_charge_counts = 0;

    std::string gpio_paper_edge;
    std::string gpio_y_home;
    bool limit_active_low = true;

    std::vector<std::string> ram_text_layers;
    std::string persistent_output_dir = "/var/lib/braillatron/documents";

    uint8_t shutdown_waveform_effect = 47;
};

TelemetryConfig load_telemetry_config(const std::string &path);

} // namespace braillatron::telemetry
