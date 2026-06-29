#include "network_util.h"

#include "shell_util.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

namespace braillatron::platform {
namespace {

constexpr const char *kWifiIface = "wlan0";
constexpr const char *kWpaCli = "/usr/sbin/wpa_cli";
constexpr const char *kIp = "/usr/bin/ip";

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

bool output_contains_yes(const std::string &output)
{
    const std::string trimmed = trim(output);
    return trimmed == "yes" || trimmed.find("yes") != std::string::npos;
}

std::string shell_quote_wpa(const std::string &value)
{
    return "'\"" + value + "\"";
}

} // namespace

bool wifi_radio_enabled()
{
    const std::string operstate =
        run_command("cat /sys/class/net/" + std::string(kWifiIface) + "/operstate 2>/dev/null");
    const std::string trimmed = trim(operstate);
    if (!trimmed.empty()) {
        return trimmed != "down";
    }

    const std::string blocked = run_command(
        "rfkill list wifi 2>/dev/null | awk '/Soft blocked:/ {print $3; exit}'");
    if (!blocked.empty()) {
        return trim(blocked) == "no";
    }

    const std::string nm = run_command("nmcli -t -f WIFI radio 2>/dev/null");
    if (!nm.empty()) {
        return trim(nm) == "enabled";
    }

    const std::string link = run_command(std::string(kIp) + " link show " + kWifiIface +
                                         " 2>/dev/null | awk '/state/ {print $9; exit}'");
    return trim(link) != "DOWN";
}

std::string set_wifi_radio_enabled(const bool enabled)
{
    const std::string result =
        run_command(std::string(kIp) + " link set " + kWifiIface + (enabled ? " up" : " down") +
                    " 2>&1");
    if (!enabled) {
        run_command(std::string(kWpaCli) + " -i " + kWifiIface + " disconnect 2>/dev/null");
    }
    run_command(enabled ? "rfkill unblock wifi 2>/dev/null" : "rfkill block wifi 2>/dev/null");
    run_command(enabled ? "nmcli radio wifi on 2>/dev/null" : "nmcli radio wifi off 2>/dev/null");
    (void)result;
    return enabled ? "WiFi on" : "WiFi off";
}

bool bluetooth_powered()
{
    const std::string output =
        run_command("bluetoothctl show 2>/dev/null | awk -F: '/Powered/ {print $2; exit}'");
    return output_contains_yes(output);
}

std::string set_bluetooth_powered(const bool enabled)
{
    run_command(std::string("bluetoothctl power ") + (enabled ? "on" : "off") + " 2>&1");
    return enabled ? "Bluetooth on" : "Bluetooth off";
}

std::vector<WifiNetwork> scan_wifi_networks(const std::string &iface)
{
    run_command(std::string(kWpaCli) + " -i " + iface + " scan >/dev/null 2>&1");
    run_command("sleep 2");
    const std::string scan =
        run_command(std::string(kWpaCli) + " -i " + iface + " scan_results 2>/dev/null");

    std::vector<WifiNetwork> networks;
    std::set<std::string> seen;
    std::istringstream stream(scan);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line.rfind("bssid", 0) == 0) {
            continue;
        }
        std::istringstream fields(line);
        std::string bssid;
        std::string frequency;
        std::string signal;
        std::string flags;
        std::string ssid;
        if (!std::getline(fields, bssid, '\t') || !std::getline(fields, frequency, '\t') ||
            !std::getline(fields, signal, '\t') || !std::getline(fields, flags, '\t') ||
            !std::getline(fields, ssid)) {
            continue;
        }
        ssid = trim(ssid);
        if (ssid.empty() || seen.count(ssid) != 0) {
            continue;
        }
        seen.insert(ssid);

        WifiNetwork network;
        network.ssid = ssid;
        try {
            network.signal_dbm = std::stoi(trim(signal));
        } catch (...) {
            network.signal_dbm = -100;
        }
        network.secured = flags.find("WPA") != std::string::npos ||
                          flags.find("WEP") != std::string::npos ||
                          flags.find("SAE") != std::string::npos;
        networks.push_back(std::move(network));
    }

    std::sort(networks.begin(), networks.end(),
              [](const WifiNetwork &a, const WifiNetwork &b) {
                  return a.signal_dbm > b.signal_dbm;
              });
    return networks;
}

std::string connect_wifi_network(const std::string &iface, const std::string &ssid,
                                 const std::string &password)
{
    const std::string id_out =
        run_command(std::string(kWpaCli) + " -i " + iface + " add_network 2>/dev/null");
    const std::string net_id = trim(id_out);
    if (net_id.empty()) {
        return "Failed to add network";
    }

    const std::string ssid_q = shell_quote_wpa(ssid);
    run_command(std::string(kWpaCli) + " -i " + iface + " set_network " + net_id + " ssid " +
                ssid_q + " 2>&1");

    if (password.empty()) {
        run_command(std::string(kWpaCli) + " -i " + iface + " set_network " + net_id +
                    " key_mgmt NONE 2>&1");
    } else {
        const std::string pass_q = shell_quote_wpa(password);
        run_command(std::string(kWpaCli) + " -i " + iface + " set_network " + net_id + " psk " +
                    pass_q + " 2>&1");
    }

    run_command(std::string(kWpaCli) + " -i " + iface + " enable_network " + net_id + " 2>&1");
    const std::string result = run_command(std::string(kWpaCli) + " -i " + iface +
                                           " select_network " + net_id + " 2>&1");
    run_command(std::string(kWpaCli) + " -i " + iface + " save_config 2>/dev/null");
    const std::string trimmed = trim(result);
    return trimmed.empty() ? "Connection attempted" : trimmed;
}

} // namespace braillatron::platform
