#pragma once

#include "../hardware/hardware_config.h"
#include "../platform/device_status.h"
#include "../platform/serial_link.h"
#include "../telemetry/telemetry_config.h"
#include "../ui/output_hub.h"
#include "../ui/ui_config.h"
#include "../keyboard/keyboard_config.h"
#include "../keyboard/keyboard_service.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace braillatron::ui {

class UiApp {
public:
    UiApp(hardware::HardwareConfig hardware,
          keyboard::KeyboardConfig keyboard_config,
          telemetry::TelemetryConfig telemetry_config,
          UiConfig ui_config,
          std::string ui_config_path);

    void start();
    void stop();
    void poll();

private:
    void refresh_status(bool force_log);
    void send_heartbeat_if_due(uint64_t now_ms);
    void handle_activate(size_t index, const std::string &label);
    static uint64_t now_ms();

    hardware::HardwareConfig hardware_;
    keyboard::KeyboardConfig keyboard_config_;
    telemetry::TelemetryConfig telemetry_config_;
    UiConfig ui_config_;
    std::string ui_config_path_;

    platform::DeviceStatus device_status_;
    platform::DeviceStatusReport status_report_;
    platform::SerialLink serial_link_;

    OutputHub output_hub_;
    keyboard::KeyboardService keyboard_;

    std::atomic<bool> running_ {false};
    uint64_t last_status_probe_ms_ = 0;
    uint64_t last_heartbeat_ms_ = 0;
    bool serial_missing_announced_ = false;
};

} // namespace braillatron::ui
