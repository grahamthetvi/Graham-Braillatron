#pragma once

namespace braillatron::telemetry {

bool request_clean_shutdown();
bool request_clean_reboot();
/** Ask systemd to restart braillatron-ui (forks systemctl). */
bool request_ui_restart();

} // namespace braillatron::telemetry
