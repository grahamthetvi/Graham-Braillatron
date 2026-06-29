#pragma once

#include <string>
#include <vector>

namespace braillatron::platform {

struct WifiNetwork {
    std::string ssid;
    int signal_dbm = 0;
    bool secured = false;
};

bool wifi_radio_enabled();
std::string set_wifi_radio_enabled(bool enabled);

bool bluetooth_powered();
std::string set_bluetooth_powered(bool enabled);

std::vector<WifiNetwork> scan_wifi_networks(const std::string &iface = "wlan0");

std::string connect_wifi_network(const std::string &iface, const std::string &ssid,
                                 const std::string &password);

} // namespace braillatron::platform
