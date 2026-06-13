#pragma once

#include "../hardware/hardware_config.h"
#include "../telemetry/telemetry_config.h"
#include "../ui/ui_config.h"
#include "../ui/display/display_config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace braillatron::platform {

enum class DeviceState {
    Connected,
    Missing,
    Degraded,
    Unconfigured,
};

struct DeviceEntry {
    std::string id;
    std::string label;
    DeviceState state = DeviceState::Missing;
    std::string detail;
};

struct DeviceStatusReport {
    std::vector<DeviceEntry> devices;

    const DeviceEntry *find(const std::string &id) const;
    std::string summary_line() const;
    std::vector<std::string> missing_user_messages() const;
};

class DeviceStatus {
public:
    DeviceStatusReport probe(const hardware::HardwareConfig &hardware,
                             const telemetry::TelemetryConfig &telemetry,
                             const ui::UiConfig &ui_config,
                             const ui::DisplayConfig &display_config);

    void log_report(const DeviceStatusReport &report, bool force = false) const;

private:
    DeviceEntry probe_serial(const std::string &device_path) const;
    DeviceEntry probe_i2c_bus(const std::string &bus_path) const;
    DeviceEntry probe_i2c_device(const std::string &bus_path, uint8_t address,
                                 const std::string &id, const std::string &label) const;
    DeviceEntry probe_gpio_path(const std::string &path, const std::string &id,
                                const std::string &label) const;
    DeviceEntry probe_file_path(const std::string &path, const std::string &id,
                                const std::string &label) const;
    DeviceEntry probe_spd_socket() const;
    DeviceEntry probe_brlapi() const;

    mutable std::string last_logged_summary_;
};

const char *device_state_label(DeviceState state);

} // namespace braillatron::platform
