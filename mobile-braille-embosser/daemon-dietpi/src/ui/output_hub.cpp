#include "output_hub.h"

extern "C" {
#include "protocol.h"
}

#include <iostream>
#include <sstream>

namespace braillatron::ui {

OutputHub::OutputHub(UiConfig ui_config, telemetry::TelemetryConfig telemetry_config)
    : ui_config_(std::move(ui_config))
    , telemetry_config_(std::move(telemetry_config))
    , menu_overlay_({"Document", "Settings", "System Status", "Emboss", "Power"})
    , tts_(create_tts_backend(ui_config_))
    , braille_(create_braille_backend(ui_config_))
    , stt_(create_stt_backend(ui_config_))
    , haptics_(create_haptic_backend(ui_config_, telemetry_config_))
{
}

OutputHub::~OutputHub() = default;

void OutputHub::emit(const std::string &message)
{
    std::cerr << "[ui] " << message << "\n";
    if (tts_ != nullptr) {
        tts_->speak(message);
    }
    if (braille_ != nullptr) {
        braille_->write(message);
    }
}

void OutputHub::announce_startup(const platform::DeviceStatusReport &report)
{
    emit("Braillatron ready");

    const std::vector<std::string> missing = report.missing_user_messages();
    if (missing.empty()) {
        emit("All devices connected");
        return;
    }

    for (const std::string &message : missing) {
        emit(message);
    }
}

void OutputHub::announce_focus(const std::string &label, bool at_boundary)
{
    if (label.empty()) {
        return;
    }

    emit(label);
    if (at_boundary && haptics_ != nullptr) {
        haptics_->play_effect(ui_config_.boundary_haptic_effect);
    }
}

void OutputHub::announce_message(const std::string &message)
{
    emit(message);
}

void OutputHub::announce_status_report(const platform::DeviceStatusReport &report)
{
    std::ostringstream stream;
    stream << "System status. ";
    stream << report.summary_line();
    emit(stream.str());
}

void OutputHub::on_shift_tts_toggle(bool pressed)
{
    if (tts_ == nullptr) {
        return;
    }

    if (pressed) {
        tts_->pause();
        emit("Speech paused");
    } else {
        tts_->resume();
        emit("Speech resumed");
    }
}

void OutputHub::on_speech_ptt_gate(bool open)
{
    if (stt_ != nullptr) {
        stt_->set_ptt_open(open);
    }

    if (open) {
        emit("Dictation listening");
    }
}

void OutputHub::on_menu_overlay(bool open)
{
    if (!open) {
        if (menu_overlay_.is_open()) {
            menu_overlay_.close();
            emit("Menu closed");
        }
        return;
    }

    menu_overlay_.open();
    emit("Menu");
    announce_focus(menu_overlay_.focused_label(), false);
}

void OutputHub::on_menu_move(bool up)
{
    if (!menu_overlay_.is_open()) {
        return;
    }

    const std::string before = menu_overlay_.focused_label();
    if (up) {
        menu_overlay_.move_up();
    } else {
        menu_overlay_.move_down();
    }

    const bool at_boundary = menu_overlay_.focused_label() == before;
    announce_focus(menu_overlay_.focused_label(), at_boundary);
}

void OutputHub::on_menu_activate()
{
    if (!menu_overlay_.is_open()) {
        return;
    }

    const std::string label = menu_overlay_.focused_label();
    menu_overlay_.activate();
    menu_overlay_.close();
    emit(label + " selected");
}

void OutputHub::announce_safety_fault(uint8_t fault_code, uint8_t severity, uint16_t detail)
{
    (void)detail;

    std::string message;
    switch (fault_code) {
    case BRAILLATRON_FAULT_FREEFALL:
        message = "Drop detected. Motors stopped for safety.";
        break;
    case BRAILLATRON_FAULT_WATCHDOG_TIMEOUT:
        message = "Controller watchdog timeout. Motors stopped.";
        break;
    case BRAILLATRON_FAULT_COMMS_LOSS:
        message = "Controller lost contact with the system. Motors stopped.";
        break;
    case BRAILLATRON_FAULT_BATTERY_CRITICAL:
        message = "Battery critically low.";
        break;
    case BRAILLATRON_FAULT_THERMAL:
        message = "Temperature fault. Motors stopped.";
        break;
    case BRAILLATRON_FAULT_ESTOP:
        message = "Emergency stop engaged.";
        break;
    case BRAILLATRON_FAULT_SENSOR_FAILURE:
        message = "Drop sensor unavailable. Motion disabled for safety.";
        break;
    default:
        message = "Safety fault reported by controller.";
        break;
    }

    if (severity >= BRAILLATRON_SEVERITY_CRITICAL) {
        message += " Embossing is paused.";
    }

    emit(message);
    if (haptics_ != nullptr) {
        haptics_->play_effect(ui_config_.boundary_haptic_effect);
    }
}

MenuOverlay &OutputHub::menu_overlay()
{
    return menu_overlay_;
}

} // namespace braillatron::ui
