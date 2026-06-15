#pragma once

#include <string>

namespace braillatron::ui {

bool send_display_pairing_command(const std::string &cmd_socket, const std::string &code);

} // namespace braillatron::ui
