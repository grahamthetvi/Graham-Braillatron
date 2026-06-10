#include "output_hub.h"

extern "C" {
#include "protocol.h"
}

#include <iostream>
#include <sstream>
#include <utility>

namespace braillatron::ui {

OutputHub::OutputHub(UiConfig ui_config, telemetry::TelemetryConfig telemetry_config,
                     std::string ui_config_path)
    : ui_config_(std::move(ui_config))
    , telemetry_config_(std::move(telemetry_config))
    , ui_config_path_(std::move(ui_config_path))
    , tts_(create_tts_backend(ui_config_))
    , braille_(create_braille_backend(ui_config_))
    , stt_(create_stt_backend(ui_config_))
    , haptics_(create_haptic_backend(ui_config_, telemetry_config_))
{
    menu_overlay_.set_root_items(build_root_menu());
}

OutputHub::~OutputHub() = default;

void OutputHub::emit(const std::string &message)
{
    std::cerr << "[ui] " << message << "\n";
    if (ui_config_.tts_enabled && tts_ != nullptr) {
        tts_->speak(message);
    }
    if (ui_config_.braille_enabled && braille_ != nullptr) {
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
    if (at_boundary && ui_config_.haptics_enabled && haptics_ != nullptr) {
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
    if (!ui_config_.tts_enabled || tts_ == nullptr) {
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
    if (!ui_config_.stt_enabled) {
        return;
    }

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

    const size_t depth_before = menu_overlay_.depth();
    menu_overlay_.activate();
    if (menu_overlay_.depth() > depth_before) {
        announce_focus(menu_overlay_.focused_label(), false);
    }
}

void OutputHub::on_menu_back()
{
    if (!menu_overlay_.is_open()) {
        return;
    }

    if (menu_overlay_.pop_level()) {
        announce_focus(menu_overlay_.focused_label(), false);
        return;
    }

    on_menu_overlay(false);
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
    if (ui_config_.haptics_enabled && haptics_ != nullptr) {
        haptics_->play_effect(ui_config_.boundary_haptic_effect);
    }
}

void OutputHub::set_status_report_provider(std::function<void()> provider)
{
    status_report_provider_ = std::move(provider);
}

MenuOverlay &OutputHub::menu_overlay()
{
    return menu_overlay_;
}

void OutputHub::persist_ui_config()
{
    if (ui_config_path_.empty()) {
        return;
    }
    save_ui_config(ui_config_path_, ui_config_);
}

void OutputHub::toggle_bool(bool &field, const char *name)
{
    field = !field;
    persist_ui_config();

    if (name == std::string("Speech to Text") && !field && stt_ != nullptr) {
        stt_->set_ptt_open(false);
    }

    std::ostringstream stream;
    stream << name << ": " << (field ? "On" : "Off");
    emit(stream.str());
}

std::vector<MenuItem> OutputHub::build_settings_menu()
{
    return {
        MenuItem {
            "TTS",
            [this]() { return ui_config_.tts_enabled ? "TTS: On" : "TTS: Off"; },
            [this](MenuOverlay &mo) {
                (void)mo;
                toggle_bool(ui_config_.tts_enabled, "TTS");
            },
        },
        MenuItem {
            "Braille",
            [this]() { return ui_config_.braille_enabled ? "Braille: On" : "Braille: Off"; },
            [this](MenuOverlay &mo) {
                (void)mo;
                toggle_bool(ui_config_.braille_enabled, "Braille");
            },
        },
        MenuItem {
            "Speech to Text",
            [this]() {
                return ui_config_.stt_enabled ? "Speech to Text: On" : "Speech to Text: Off";
            },
            [this](MenuOverlay &mo) {
                (void)mo;
                toggle_bool(ui_config_.stt_enabled, "Speech to Text");
            },
        },
        MenuItem {
            "Haptics",
            [this]() { return ui_config_.haptics_enabled ? "Haptics: On" : "Haptics: Off"; },
            [this](MenuOverlay &mo) {
                (void)mo;
                toggle_bool(ui_config_.haptics_enabled, "Haptics");
            },
        },
    };
}

std::vector<MenuItem> OutputHub::build_root_menu()
{
    return {
        MenuItem {
            "Document",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                emit("Document selected");
                menu_overlay_.close();
            },
        },
        MenuItem {
            "Settings",
            {},
            [this](MenuOverlay &mo) { mo.push_level(build_settings_menu()); },
        },
        MenuItem {
            "System Status",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                if (status_report_provider_) {
                    status_report_provider_();
                }
            },
        },
        MenuItem {
            "Emboss",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                emit("Emboss selected");
                menu_overlay_.close();
            },
        },
        MenuItem {
            "Power",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                emit("Power selected");
                menu_overlay_.close();
            },
        },
    };
}

} // namespace braillatron::ui
