#pragma once

#include "telemetry_sentinel.h"

#include <string>

namespace braillatron::telemetry {

constexpr const char *kTelemetryJsonPath = "/run/braillatron/telemetry.json";

bool write_telemetry_json(const std::string &path, const TelemetrySnapshot &snapshot);
TelemetrySnapshot read_telemetry_json(const std::string &path);
void sync_motion_gate_from_telemetry(const std::string &path = kTelemetryJsonPath);

} // namespace braillatron::telemetry
