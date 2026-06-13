#include "hardware/hardware_config.h"
#include "kinematics/kinematics_config.h"
#include "telemetry/telemetry_config.h"
#include "ui/ui_app.h"
#include "ui/ui_config.h"
#include "keyboard/keyboard_config.h"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t g_running = 1;

void handle_signal(int)
{
    g_running = 0;
}

std::string config_dir()
{
    const char *env = std::getenv("BRAILLATRON_CONFIG");
    if (env != nullptr && env[0] != '\0') {
        return env;
    }
    return "config";
}

std::string resolve_config_path(const std::string &base, const std::string &path)
{
    if (path.empty() || path[0] == '/') {
        return path;
    }
    return base + "/" + path;
}

} // namespace

int main(int argc, char *argv[])
{
    std::cerr << std::unitbuf;
    const std::string base = config_dir();
    const std::string hardware_path =
        argc > 1 ? argv[1] : resolve_config_path(base, "hardware.conf");

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    try {
        std::cerr << "[ui] loading configuration from " << base << "\n";
        const braillatron::hardware::HardwareConfig hardware =
            braillatron::hardware::load_hardware_config(hardware_path);

        braillatron::hardware::HardwareConfig resolved_hardware = hardware;
        resolved_hardware.matrix_map_config =
            resolve_config_path(base, hardware.matrix_map_config);
        resolved_hardware.telemetry_config =
            resolve_config_path(base, hardware.telemetry_config);

        braillatron::keyboard::KeyboardConfig keyboard_config =
            braillatron::keyboard::load_keyboard_config(
                resolve_config_path(base, "keyboard.conf"));
        keyboard_config.board_profile = resolved_hardware.board_profile;
        keyboard_config.serial_device = resolved_hardware.arduino_device;
        keyboard_config.baud_rate = resolved_hardware.baud_rate;
        keyboard_config.matrix_map_config = resolved_hardware.matrix_map_config;
        keyboard_config.allow_missing_arduino = resolved_hardware.allow_missing_arduino;

        const braillatron::telemetry::TelemetryConfig telemetry_config =
            braillatron::telemetry::load_telemetry_config(resolved_hardware.telemetry_config);

        const braillatron::kinematics::KinematicsConfig kinematics_config =
            braillatron::kinematics::load_kinematics_config(
                resolve_config_path(base, "kinematics.conf"));

        const std::string ui_config_path = resolve_config_path(base, "ui.conf");
        const braillatron::ui::UiConfig ui_config =
            braillatron::ui::load_ui_config(ui_config_path);

        braillatron::ui::UiApp app(resolved_hardware, keyboard_config, telemetry_config,
                                   kinematics_config, ui_config, ui_config_path);
        std::cerr << "[ui] starting services\n";
        app.start();

        std::cerr << "braillatron-ui profile=" << resolved_hardware.board_profile << "\n";

        while (g_running) {
            app.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        app.stop();
    } catch (const std::exception &ex) {
        std::cerr << "braillatron-ui error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
