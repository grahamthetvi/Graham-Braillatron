#include "crash_reporter.h"

#include <csignal>
#include <cstdlib>
#include <iostream>

namespace braillatron::telemetry {

namespace {

CrashReporterConfig g_config;

void on_fatal_signal(int signum)
{
    std::cerr << "[crash] signal " << signum;
    if (!g_config.sentry_dsn.empty()) {
        std::cerr << " (sentry configured)";
    }
    if (!g_config.memfault_project_key.empty()) {
        std::cerr << " (memfault configured)";
    }
    std::cerr << " build=" << g_config.build_version << "\n";
    std::_Exit(128 + signum);
}

} // namespace

void install_crash_reporter(const CrashReporterConfig &config)
{
    g_config = config;
    if (g_config.sentry_dsn.empty() && g_config.memfault_project_key.empty()) {
        return;
    }

    std::signal(SIGSEGV, on_fatal_signal);
    std::signal(SIGABRT, on_fatal_signal);
    std::cerr << "[crash] reporter armed (privacy-scrubbed, no document paths)\n";
}

} // namespace braillatron::telemetry
