#include "system_shutdown.h"

#include <cstdlib>
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

bool request_ui_restart()
{
    const pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        execl("/bin/systemctl", "systemctl", "restart", "braillatron-ui.service",
              static_cast<char *>(nullptr));
        _exit(127);
    }
    return true;
}

} // namespace braillatron::telemetry
