#include "connect_defaults.h"

namespace braillatron::connect {

ConnectConfig default_connect_config()
{
    const std::string base = config_dir_from_env();
    return load_connect_config(resolve_config_path(base, "connect.conf"));
}

std::string default_connect_socket_path()
{
    return default_connect_config().socket_path;
}

} // namespace braillatron::connect
