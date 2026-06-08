#include "hardware/hardware_config.h"
#include "platform/device_status.h"
#include "telemetry/telemetry_config.h"
#include "ui/output_hub.h"
#include "ui/ui_config.h"

#include <iostream>

int main()
{
    braillatron::hardware::HardwareConfig hardware {};
    hardware.allow_missing_arduino = true;

    braillatron::telemetry::TelemetryConfig telemetry {};
    braillatron::ui::UiConfig ui_config {};

    braillatron::platform::DeviceStatus status;
    const braillatron::platform::DeviceStatusReport report =
        status.probe(hardware, telemetry, ui_config);

    status.log_report(report, true);

    braillatron::ui::OutputHub hub(ui_config, telemetry);
    hub.announce_startup(report);
    hub.announce_focus("Document", false);
    hub.announce_status_report(report);

    const std::vector<std::string> missing = report.missing_user_messages();
    if (missing.empty()) {
        std::cerr << "ui self-test failed: expected missing devices on host\n";
        return 1;
    }

    std::cout << "ui self-test ok\n";
    return 0;
}
