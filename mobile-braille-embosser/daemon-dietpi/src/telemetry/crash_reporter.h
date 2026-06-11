#pragma once

#include <string>

namespace braillatron::telemetry {

struct CrashReporterConfig {
    std::string sentry_dsn;
    std::string memfault_project_key;
    std::string build_version = "braillatron-dev";
};

void install_crash_reporter(const CrashReporterConfig &config);

} // namespace braillatron::telemetry
