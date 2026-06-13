#include "ui_app.h"

#include "../keyboard/global_hooks.h"
#include "../telemetry/telemetry_bridge.h"

#include <chrono>
#include <iostream>

namespace braillatron::ui {

UiApp::UiApp(hardware::HardwareConfig hardware,
             keyboard::KeyboardConfig keyboard_config,
             telemetry::TelemetryConfig telemetry_config,
             kinematics::KinematicsConfig kinematics_config,
             UiConfig ui_config,
             std::string ui_config_path,
             DisplayConfig display_config)
    : hardware_(std::move(hardware))
    , keyboard_config_(std::move(keyboard_config))
    , telemetry_config_(std::move(telemetry_config))
    , ui_config_(std::move(ui_config))
    , ui_config_path_(std::move(ui_config_path))
    , display_config_(std::move(display_config))
    , braille_service_(documents::braille_grade_preset_from_string(ui_config_.braille_table))
    , serial_link_(hardware_.arduino_device, hardware_.baud_rate)
    , motion_service_(std::move(kinematics_config))
    , brf_store_("/var/lib/braillatron/ram/layer1.brf")
    , coord_store_("/var/lib/braillatron/ram/coords.json")
    , output_hub_(ui_config_, telemetry_config_, ui_config_path_, display_config_,
                  &motion_service_, &braille_service_)
    , connect_client_(braillatron::connect::default_connect_config().socket_path,
                      braillatron::connect::default_connect_config().event_path)
    , keyboard_(keyboard_config_)
{
    coord_store_.load();
    brf_store_.load();

    paper_separator_.set_feed_handler([this](int32_t delta) { motion_service_.feed_lines(delta); });

    ui_context_.output = &output_hub_;
    ui_context_.motion = &motion_service_;
    ui_context_.brf = &brf_store_;
    ui_context_.coords = &coord_store_;
    ui_context_.edit = &edit_session_;
    ui_context_.paper_sep = &paper_separator_;
    ui_context_.braille = &braille_service_;
    ui_context_.registry = &app_registry_;
    ui_context_.connect = &connect_client_;

    app_registry_.set_context(ui_context_);
    output_hub_.set_app_registry(&app_registry_);
    output_hub_.set_connect_client(&connect_client_);

    hooks::set_output_hub(&output_hub_);
    hooks::set_app_registry(&app_registry_);

    keyboard_.set_braille_service(&braille_service_);

    motion_service_.reset_from_coordinate(coord_store_.state().x_microsteps,
                                          coord_store_.state().y_line_index);

    output_hub_.set_status_report_provider([this] {
        output_hub_.announce_status_report(status_report_);
    });

    keyboard_.focus_nav().set_entries(app_registry_.launcher_labels());
    output_hub_.set_focus_nav(&keyboard_.focus_nav());

    keyboard_.focus_nav().set_focus_changed_handler(
        [this](const std::string &label, bool at_boundary) {
            output_hub_.announce_focus(label, at_boundary);
        });

    keyboard_.focus_nav().set_activate_handler(
        [this](size_t index, const std::string &label) { handle_activate(index, label); });
}

void UiApp::start()
{
    if (running_.load()) {
        return;
    }

    running_ = true;
    refresh_status(true);
    output_hub_.announce_startup(status_report_);
    output_hub_.announce_focus(keyboard_.focus_nav().focused_label(), false);
    keyboard_.start();
}

void UiApp::stop()
{
    running_ = false;
    app_registry_.exit();
    brf_store_.save();
    coord_store_.save();
    serial_link_.close();
    output_hub_.release_backends();
    keyboard_.stop();
    hooks::set_app_registry(nullptr);
    hooks::set_output_hub(nullptr);
}

void UiApp::poll()
{
    if (!running_.load()) {
        return;
    }

    const uint64_t now = now_ms();
    keyboard_.poll();
    app_registry_.poll();
    refresh_status(false);
    telemetry::sync_motion_gate_from_telemetry();
    output_hub_.check_battery_warning();
    send_heartbeat_if_due(now);

    if (!keyboard_.serial_connected() && keyboard_config_.allow_missing_arduino) {
        keyboard_.try_serial_reconnect();
    }
}

void UiApp::refresh_status(bool force_log)
{
    const uint64_t now = now_ms();
    if (!force_log && now - last_status_probe_ms_ < ui_config_.status_probe_interval_ms) {
        return;
    }

    last_status_probe_ms_ = now;
    status_report_ = device_status_.probe(hardware_, telemetry_config_, ui_config_, display_config_);
    device_status_.log_report(status_report_, force_log);

    if (!keyboard_.serial_connected() && !serial_missing_announced_) {
        serial_missing_announced_ = true;
        output_hub_.announce_message("Arduino keyboard link not connected");
    }

    if (keyboard_.serial_connected()) {
        serial_missing_announced_ = false;
        if (!serial_link_.is_open()) {
            serial_link_.try_open();
        }
    } else {
        serial_link_.close();
    }
}

void UiApp::send_heartbeat_if_due(uint64_t now_ms)
{
    if (!serial_link_.is_open()) {
        return;
    }

    if (now_ms - last_heartbeat_ms_ < ui_config_.heartbeat_interval_ms) {
        return;
    }

    last_heartbeat_ms_ = now_ms;
    if (!serial_link_.send_heartbeat()) {
        serial_link_.close();
    }
}

void UiApp::handle_activate(size_t index, const std::string &label)
{
    (void)index;

    if (label == "Settings") {
        output_hub_.on_menu_overlay(true);
        return;
    }

    if (label == "Power") {
        output_hub_.request_shutdown();
        return;
    }

    const std::string app_id = app_registry_.launcher_id_for_label(label);
    if (!app_id.empty()) {
        app_registry_.enter(app_id);
        return;
    }

    output_hub_.announce_message(label + " selected");
}

uint64_t UiApp::now_ms()
{
    using clock = std::chrono::steady_clock;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch())
            .count());
}

} // namespace braillatron::ui
