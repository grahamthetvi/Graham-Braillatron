#pragma once

#include "../documents/brf_store.h"
#include "../documents/coordinate_state.h"
#include "../documents/edit_session.h"
#include "../documents/paper_separator.h"
#include "../hardware/hardware_config.h"
#include "../kinematics/kinematics_config.h"
#include "../motion/motion_service.h"
#include "../platform/device_status.h"
#include "../platform/serial_link.h"
#include "../telemetry/telemetry_config.h"
#include "../ui/apps/app_registry.h"
#include "../ui/apps/ui_context.h"
#include "../ui/output_hub.h"
#include "../connect/connect_client.h"
#include "../connect/connect_defaults.h"
#include "../documents/liblouis_bridge.h"
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
          kinematics::KinematicsConfig kinematics_config,
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
    documents::BrailleTranslationService braille_service_;

    platform::DeviceStatus device_status_;
    platform::DeviceStatusReport status_report_;
    platform::SerialLink serial_link_;

    motion::MotionService motion_service_;
    documents::BrfStore brf_store_;
    documents::CoordinateStore coord_store_;
    documents::EditSession edit_session_;
    documents::PaperSeparator paper_separator_;

    OutputHub output_hub_;
    connect::ConnectClient connect_client_;
    AppRegistry app_registry_;
    UiContext ui_context_;
    keyboard::KeyboardService keyboard_;

    std::atomic<bool> running_ {false};
    uint64_t last_status_probe_ms_ = 0;
    uint64_t last_heartbeat_ms_ = 0;
    bool serial_missing_announced_ = false;
};

} // namespace braillatron::ui
