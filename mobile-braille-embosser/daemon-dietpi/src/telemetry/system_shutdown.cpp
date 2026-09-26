#include "system_shutdown.h"

#include <cstdlib>
#include <unistd.h>

namespace braillatron::telemetry {

namespace {

// Fork before exec so the caller keeps running: UiApp::stop() must still
// save BRF/coordinate state and finish TTS while systemd shuts us down.
bool run_shutdown_command(const char *mode_flag)
{
    const pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        execl("/usr/sbin/shutdown", "shutdown", mode_flag, "now",
              static_cast<char *>(nullptr));
        _exit(127);
    }
    return true;
}

} // namespace

bool request_clean_shutdown()
{
    return run_shutdown_command("-h");
}

bool request_clean_reboot()
{
    return run_shutdown_command("-r");
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
