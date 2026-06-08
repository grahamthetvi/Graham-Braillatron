#include "output_hub.h"

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
        return;
    }

    menu_overlay_.open();
    emit("Menu");
    announce_focus(menu_overlay_.focused_label(), false);
}

MenuOverlay &OutputHub::menu_overlay()
{
    return menu_overlay_;
}

} // namespace braillatron::ui
