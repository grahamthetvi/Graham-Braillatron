#pragma once

#include "connect_config.h"

#include <string>

namespace braillatron::connect {

ConnectConfig default_connect_config();
std::string default_connect_socket_path();

} // namespace braillatron::connect
