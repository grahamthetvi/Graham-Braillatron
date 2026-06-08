#include "device_status.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace braillatron::platform {

namespace {

bool path_exists(const std::string &path)
{
    return !path.empty() && access(path.c_str(), F_OK) == 0;
}

} // namespace

const char *device_state_label(DeviceState state)
{
    switch (state) {
    case DeviceState::Connected:
        return "connected";
    case DeviceState::Missing:
        return "missing";
    case DeviceState::Degraded:
        return "degraded";
    case DeviceState::Unconfigured:
        return "unconfigured";
    }
    return "unknown";
}

const DeviceEntry *DeviceStatusReport::find(const std::string &id) const
{
    for (const DeviceEntry &entry : devices) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

std::string DeviceStatusReport::summary_line() const
{
    std::string line;
    for (size_t i = 0; i < devices.size(); ++i) {
        if (i > 0) {
            line += ", ";
        }
        line += devices[i].label;
        line += ": ";
        line += device_state_label(devices[i].state);
    }
    return line;
}

std::vector<std::string> DeviceStatusReport::missing_user_messages() const
{
    std::vector<std::string> messages;
    for (const DeviceEntry &entry : devices) {
        if (entry.state == DeviceState::Missing || entry.state == DeviceState::Degraded) {
            messages.push_back(entry.label + " not available" +
                               (entry.detail.empty() ? "" : " (" + entry.detail + ")"));
        }
    }
    return messages;
}

DeviceEntry DeviceStatus::probe_serial(const std::string &device_path) const
{
    DeviceEntry entry {};
    entry.id = "arduino_serial";
    entry.label = "Arduino keyboard link";

    if (device_path.empty()) {
        entry.state = DeviceState::Unconfigured;
        entry.detail = "no device path configured";
        return entry;
    }

    entry.detail = device_path;
    const int fd = open(device_path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        entry.state = DeviceState::Missing;
        entry.detail = device_path + ": " + std::strerror(errno);
        return entry;
    }

    close(fd);
    entry.state = DeviceState::Connected;
    return entry;
}

DeviceEntry DeviceStatus::probe_i2c_bus(const std::string &bus_path) const
{
    DeviceEntry entry {};
    entry.id = "i2c_bus";
    entry.label = "I2C bus";

    if (bus_path.empty()) {
        entry.state = DeviceState::Unconfigured;
        return entry;
    }

    const int fd = open(bus_path.c_str(), O_RDWR);
    if (fd < 0) {
        entry.state = DeviceState::Missing;
        entry.detail = bus_path + ": " + std::strerror(errno);
        return entry;
    }

    close(fd);
    entry.state = DeviceState::Connected;
    entry.detail = bus_path;
    return entry;
}

DeviceEntry DeviceStatus::probe_i2c_device(const std::string &bus_path, uint8_t address,
                                           const std::string &id, const std::string &label) const
{
    DeviceEntry entry {};
    entry.id = id;
    entry.label = label;

    const int fd = open(bus_path.c_str(), O_RDWR);
    if (fd < 0) {
        entry.state = DeviceState::Missing;
        entry.detail = "bus unavailable";
        return entry;
    }

    if (ioctl(fd, I2C_SLAVE, address) < 0) {
        close(fd);
        entry.state = DeviceState::Missing;
        entry.detail = "address 0x" + std::to_string(address);
        return entry;
    }

    close(fd);
    entry.state = DeviceState::Connected;
    return entry;
}

DeviceEntry DeviceStatus::probe_gpio_path(const std::string &path, const std::string &id,
                                          const std::string &label) const
{
    DeviceEntry entry {};
    entry.id = id;
    entry.label = label;

    if (path.empty()) {
        entry.state = DeviceState::Unconfigured;
        entry.detail = "path not configured";
        return entry;
    }

    if (!path_exists(path)) {
        entry.state = DeviceState::Missing;
        entry.detail = path;
        return entry;
    }

    entry.state = DeviceState::Connected;
    entry.detail = path;
    return entry;
}

DeviceEntry DeviceStatus::probe_file_path(const std::string &path, const std::string &id,
                                          const std::string &label) const
{
    DeviceEntry entry {};
    entry.id = id;
    entry.label = label;

    if (path.empty()) {
        entry.state = DeviceState::Unconfigured;
        return entry;
    }

    struct stat st {};
    if (stat(path.c_str(), &st) != 0) {
        entry.state = DeviceState::Missing;
        entry.detail = path;
        return entry;
    }

    entry.state = DeviceState::Connected;
    entry.detail = path;
    return entry;
}

DeviceEntry DeviceStatus::probe_spd_socket() const
{
    DeviceEntry entry {};
    entry.id = "speech_dispatcher";
    entry.label = "Speech Dispatcher";

    const char *socket_paths[] = {
        "/run/speech-dispatcher/speechd.sock",
        "/var/run/speech-dispatcher/speechd.sock",
    };

    for (const char *path : socket_paths) {
        if (path_exists(path)) {
            entry.state = DeviceState::Connected;
            entry.detail = path;
            return entry;
        }
    }

    entry.state = DeviceState::Missing;
    entry.detail = "spd socket not found";
    return entry;
}

DeviceEntry DeviceStatus::probe_brlapi() const
{
    DeviceEntry entry {};
    entry.id = "brltty";
    entry.label = "BRLTTY";

    if (path_exists("/var/lib/brltty/brltty.pid") || path_exists("/run/brltty.pid")) {
        entry.state = DeviceState::Connected;
        return entry;
    }

    entry.state = DeviceState::Missing;
    entry.detail = "brltty not running";
    return entry;
}

DeviceStatusReport DeviceStatus::probe(const hardware::HardwareConfig &hardware,
                                       const telemetry::TelemetryConfig &telemetry,
                                       const ui::UiConfig &ui_config)
{
    DeviceStatusReport report;

    report.devices.push_back(probe_serial(hardware.arduino_device));
    report.devices.push_back(probe_i2c_bus(telemetry.i2c_bus));
    report.devices.push_back(probe_i2c_device(
        telemetry.i2c_bus, telemetry.ltc2944_address, "ltc2944", "Battery gauge"));
    report.devices.push_back(probe_i2c_device(
        telemetry.i2c_bus, telemetry.drv2605l_address, "drv2605l", "Haptic driver"));
    report.devices.push_back(probe_gpio_path(
        telemetry.gpio_paper_edge, "limit_paper_edge", "Paper edge sensor"));
    report.devices.push_back(probe_gpio_path(
        telemetry.gpio_y_home, "limit_y_home", "Y-axis home sensor"));
    report.devices.push_back(probe_file_path(
        ui_config.vosk_model_path, "vosk_model", "Vosk speech model"));
    report.devices.push_back(probe_spd_socket());
    report.devices.push_back(probe_brlapi());

    return report;
}

void DeviceStatus::log_report(const DeviceStatusReport &report, bool force) const
{
    const std::string summary = report.summary_line();
    if (!force && summary == last_logged_summary_) {
        return;
    }

    last_logged_summary_ = summary;
    for (const DeviceEntry &entry : report.devices) {
        std::cerr << "[status] " << entry.id << ": " << device_state_label(entry.state);
        if (!entry.detail.empty()) {
            std::cerr << " (" << entry.detail << ")";
        }
        std::cerr << "\n";
    }
}

} // namespace braillatron::platform
