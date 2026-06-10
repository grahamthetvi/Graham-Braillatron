#pragma once

#include "../platform/device_status.h"
#include "backends/backend.h"
#include "menu_overlay.h"
#include "ui_config.h"

#include "../telemetry/telemetry_config.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {

class OutputHub {
public:
    OutputHub(UiConfig ui_config, telemetry::TelemetryConfig telemetry_config,
              std::string ui_config_path);
    ~OutputHub();

    OutputHub(const OutputHub &) = delete;
    OutputHub &operator=(const OutputHub &) = delete;

    void announce_startup(const platform::DeviceStatusReport &report);
    void announce_focus(const std::string &label, bool at_boundary);
    void announce_message(const std::string &message);
    void announce_status_report(const platform::DeviceStatusReport &report);

    void on_shift_tts_toggle(bool pressed);
    void on_speech_ptt_gate(bool open);
    void on_menu_overlay(bool open);
    void on_menu_move(bool up);
    void on_menu_activate();
    void on_menu_back();

    void announce_safety_fault(uint8_t fault_code, uint8_t severity, uint16_t detail);

    void set_status_report_provider(std::function<void()> provider);

    MenuOverlay &menu_overlay();

private:
    void emit(const std::string &message);
    void persist_ui_config();
    void toggle_bool(bool &field, const char *name);
    std::vector<MenuItem> build_settings_menu();
    std::vector<MenuItem> build_root_menu();

    UiConfig ui_config_;
    telemetry::TelemetryConfig telemetry_config_;
    std::string ui_config_path_;
    MenuOverlay menu_overlay_;
    std::function<void()> status_report_provider_;

    std::unique_ptr<TtsBackend> tts_;
    std::unique_ptr<BrailleBackend> braille_;
    std::unique_ptr<SttBackend> stt_;
    std::unique_ptr<HapticBackend> haptics_;
};

} // namespace braillatron::ui
