#include "system_shutdown.h"

#include <unistd.h>

namespace braillatron::telemetry {

bool request_clean_shutdown()
{
    const char *shutdown_path = "/usr/sbin/shutdown";
    const char *argv[] = {"/usr/sbin/shutdown", "-h", "now", nullptr};
    execv(shutdown_path, const_cast<char *const *>(argv));
    return false;
}

bool request_clean_reboot()
{
    const char *shutdown_path = "/usr/sbin/shutdown";
    const char *argv[] = {"/usr/sbin/shutdown", "-r", "now", nullptr};
    execv(shutdown_path, const_cast<char *const *>(argv));
    return false;
}

} // namespace braillatron::telemetry
