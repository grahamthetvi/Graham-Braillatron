#include "ui_app.h"

#include "../keyboard/global_hooks.h"

#include <chrono>
#include <iostream>

namespace braillatron::ui {

UiApp::UiApp(hardware::HardwareConfig hardware,
             keyboard::KeyboardConfig keyboard_config,
             telemetry::TelemetryConfig telemetry_config,
             UiConfig ui_config)
    : hardware_(std::move(hardware))
    , keyboard_config_(std::move(keyboard_config))
    , telemetry_config_(std::move(telemetry_config))
    , ui_config_(std::move(ui_config))
    , serial_link_(hardware_.arduino_device, hardware_.baud_rate)
    , output_hub_(ui_config_, telemetry_config_)
    , keyboard_(keyboard_config_)
{
    hooks::set_output_hub(&output_hub_);

    keyboard_.focus_nav().set_entries(
        {"Document", "Settings", "System Status", "Emboss", "Power"});

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
    serial_link_.close();
    keyboard_.stop();
    hooks::set_output_hub(nullptr);
}

void UiApp::poll()
{
    if (!running_.load()) {
        return;
    }

    const uint64_t now = now_ms();
    keyboard_.poll();
    refresh_status(false);
    send_heartbeat_if_due(now);

    if (!keyboard_.serial_connected() && keyboard_config_.allow_missing_arduino) {
        keyboard_.try_serial_reconnect();
    }
}

void UiApp::refresh_status(bool force_log)
{
    const uint64_t now = now_ms();
    if (!force_log &&
        now - last_status_probe_ms_ < ui_config_.status_probe_interval_ms) {
        return;
    }

    last_status_probe_ms_ = now;
    status_report_ = device_status_.probe(hardware_, telemetry_config_, ui_config_);
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

    if (label == "System Status") {
        output_hub_.announce_status_report(status_report_);
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
