#pragma once

#include <optional>
#include <string>

namespace braillatron::platform {

std::string read_output_mode();
std::string read_bluetooth_mac();

std::optional<std::string> normalize_mac(const std::string &input);

bool save_bluetooth_mac(const std::string &mac);

std::string switch_output(const std::string &mode);
std::string connect_bluetooth();
std::string pair_bluetooth_mac(const std::string &mac);

std::string mode_display_label(const std::string &mode);

} // namespace braillatron::platform
