#include "telemetry_config.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace braillatron::telemetry {

namespace {

std::string trim(const std::string &value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(start, end - start);
}

std::vector<std::string> split_csv(const std::string &value)
{
    std::vector<std::string> parts;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        item = trim(item);
        if (!item.empty()) {
            parts.push_back(item);
        }
    }
    return parts;
}

} // namespace

TelemetryConfig load_telemetry_config(const std::string &path)
{
    TelemetryConfig config;
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("unable to open telemetry config: " + path);
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));

        if (key == "i2c_bus") {
            config.i2c_bus = value;
        } else if (key == "ltc2944_address") {
            config.ltc2944_address = static_cast<uint8_t>(std::stoul(value, nullptr, 0));
        } else if (key == "drv2605l_address") {
            config.drv2605l_address = static_cast<uint8_t>(std::stoul(value, nullptr, 0));
        } else if (key == "battery_critical_percent") {
            config.battery_critical_percent = static_cast<uint8_t>(std::stoul(value));
        } else if (key == "poll_interval_ms") {
            config.poll_interval_ms = static_cast<uint32_t>(std::stoul(value));
        } else if (key == "battery_4s_min_mv") {
            config.battery_4s_min_mv = static_cast<uint16_t>(std::stoul(value));
        } else if (key == "battery_4s_max_mv") {
            config.battery_4s_max_mv = static_cast<uint16_t>(std::stoul(value));
        } else if (key == "ltc2944_mv_per_lsb") {
            config.ltc2944_mv_per_lsb = std::stod(value);
        } else if (key == "battery_full_charge_counts") {
            config.battery_full_charge_counts = static_cast<uint32_t>(std::stoul(value));
        } else if (key == "battery_empty_charge_counts") {
            config.battery_empty_charge_counts = static_cast<uint32_t>(std::stoul(value));
        } else if (key == "gpio_paper_edge") {
            config.gpio_paper_edge = value;
        } else if (key == "gpio_y_home") {
            config.gpio_y_home = value;
        } else if (key == "limit_active_low") {
            config.limit_active_low = value != "0";
        } else if (key == "ram_text_layers") {
            config.ram_text_layers = split_csv(value);
        } else if (key == "persistent_output_dir") {
            config.persistent_output_dir = value;
        } else if (key == "shutdown_waveform_effect") {
            config.shutdown_waveform_effect = static_cast<uint8_t>(std::stoul(value));
        }
    }

    return config;
}

} // namespace braillatron::telemetry
