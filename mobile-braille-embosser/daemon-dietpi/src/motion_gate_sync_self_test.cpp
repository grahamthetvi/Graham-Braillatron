#include "motion_gate.h"
#include "telemetry/telemetry_bridge.h"
#include "telemetry/telemetry_sentinel.h"

#include <cstdlib>
#include <iostream>
#include <string>

int main()
{
    const std::string path = "/tmp/braillatron_motion_gate_sync_test.json";

    braillatron::telemetry::TelemetrySnapshot snapshot {};
    snapshot.motion_blocked = true;
    snapshot.limit_status = BRAILLATRON_LIMIT_BATTERY_CRITICAL;

    if (!braillatron::telemetry::write_telemetry_json(path, snapshot)) {
        std::cerr << "motion gate sync test: failed to write telemetry json\n";
        return 1;
    }

    braillatron::telemetry::sync_motion_gate_from_telemetry(path);

    if (!braillatron::MotionGate::is_blocked()) {
        std::cerr << "motion gate sync test: expected MotionGate blocked after sync\n";
        return 1;
    }

    const char *reason = braillatron::MotionGate::block_reason();
    if (reason == nullptr || std::string(reason) != "battery below critical threshold") {
        std::cerr << "motion gate sync test: unexpected block reason\n";
        return 1;
    }

    std::remove(path.c_str());
    std::cout << "motion gate sync test passed\n";
    return 0;
}
