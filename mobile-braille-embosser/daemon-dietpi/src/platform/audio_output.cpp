#include "audio_output.h"

#include "shell_util.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

namespace braillatron::platform {
namespace {

constexpr const char *kAudioOutputConf = "/etc/braillatron/audio-output.conf";
constexpr const char *kBluetoothAudioConf = "/etc/braillatron/bluetooth-audio.conf";
constexpr const char *kAudioSelectScript = "/usr/local/bin/braillatron-audio-select";

std::string trim(const std::string &value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(start, end - start);
}

std::string read_conf_value(const std::string &path, const std::string &key)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return {};
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        if (trim(line.substr(0, eq)) == key) {
            return trim(line.substr(eq + 1));
        }
    }

    return {};
}

std::string collapse_command_output(const std::string &output)
{
    std::ostringstream stream;
    std::istringstream lines(output);
    std::string line;
    bool first = true;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        if (!first) {
            stream << ' ';
        }
        stream << line;
        first = false;
    }
    return stream.str();
}

} // namespace

std::string read_output_mode()
{
    const std::string mode = read_conf_value(kAudioOutputConf, "mode");
    if (mode.empty()) {
        return "aux";
    }
    return mode;
}

std::string read_bluetooth_mac()
{
    return read_conf_value(kBluetoothAudioConf, "device_mac");
}

std::vector<BluetoothDevice> scan_bluetooth_devices(const bool discover)
{
    run_command("bluetoothctl power on 2>/dev/null");
    if (discover) {
        run_command(
            "bluetoothctl --timeout 8 scan on 2>/dev/null || "
            "{ bluetoothctl scan on 2>/dev/null; sleep 8; }");
    }
    const std::string output = run_command("bluetoothctl devices 2>/dev/null");

    std::vector<BluetoothDevice> devices;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.rfind("Device ", 0) != 0) {
            continue;
        }
        const std::string rest = line.substr(7);
        const size_t name_start = rest.find(' ');
        if (name_start == std::string::npos || name_start == 0) {
            continue;
        }
        BluetoothDevice device;
        device.mac = trim(rest.substr(0, name_start));
        device.name = trim(rest.substr(name_start + 1));
        if (device.name.empty()) {
            device.name = device.mac;
        }
        devices.push_back(std::move(device));
    }
    return devices;
}

std::optional<std::string> normalize_mac(const std::string &input)
{
    std::string compact;
    compact.reserve(12);

    for (unsigned char ch : input) {
        if (ch == ':' || ch == '-' || std::isspace(ch)) {
            continue;
        }
        if (!std::isxdigit(ch)) {
            return std::nullopt;
        }
        compact.push_back(static_cast<char>(std::toupper(ch)));
    }

    if (compact.size() != 12) {
        return std::nullopt;
    }

    std::ostringstream formatted;
    for (size_t i = 0; i < compact.size(); i += 2) {
        if (i > 0) {
            formatted << ':';
        }
        formatted << compact.substr(i, 2);
    }
    return formatted.str();
}

bool save_bluetooth_mac(const std::string &mac)
{
    std::ofstream file(kBluetoothAudioConf);
    if (!file.is_open()) {
        return false;
    }

    file << "# Paired Bluetooth speaker for braillatron-audio-select bluetooth\n";
    file << "device_mac=" << mac << '\n';
    return static_cast<bool>(file);
}

std::string switch_output(const std::string &mode)
{
    if (mode == "bluetooth" && read_bluetooth_mac().empty()) {
        return "No Bluetooth speaker saved. Pair a speaker first.";
    }

    const std::string cmd =
        std::string(kAudioSelectScript) + " --no-restart-ui " + mode + " 2>&1";
    const std::string output = run_command(cmd);
    const std::string message = collapse_command_output(output);
    if (message.empty()) {
        return "Audio output switch failed. Run as root on the device.";
    }
    return message;
}

std::string connect_bluetooth()
{
    const std::string mac = read_bluetooth_mac();
    if (mac.empty()) {
        return "No Bluetooth speaker saved. Pair a speaker first.";
    }

    run_command("bluetoothctl trust " + mac + " 2>&1");
    const std::string result = run_command("bluetoothctl connect " + mac + " 2>&1");
    const std::string message = collapse_command_output(result);
    if (message.find("successful") != std::string::npos ||
        message.find("Already connected") != std::string::npos) {
        return "Bluetooth speaker connected.";
    }
    if (message.empty()) {
        return "Bluetooth connect attempted for " + mac;
    }
    return message;
}

std::string pair_bluetooth_mac(const std::string &mac)
{
    run_command("bluetoothctl trust " + mac + " 2>&1");
    run_command("bluetoothctl pair " + mac + " 2>&1");
    const std::string result = run_command("bluetoothctl connect " + mac + " 2>&1");
    const std::string message = collapse_command_output(result);
    if (message.find("successful") != std::string::npos ||
        message.find("Already connected") != std::string::npos ||
        message.find("Pairing successful") != std::string::npos) {
        return "Bluetooth speaker paired.";
    }
    if (message.empty()) {
        return "Bluetooth pairing attempted for " + mac;
    }
    return message;
}

std::string mode_display_label(const std::string &mode)
{
    if (mode == "aux") {
        return "Aux jack";
    }
    if (mode == "bluetooth" || mode == "bt") {
        return "Bluetooth";
    }
    if (mode == "i2s") {
        return "I2S amplifier";
    }
    return mode;
}

} // namespace braillatron::platform
