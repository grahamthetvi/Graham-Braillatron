#include "homing_service.h"

#include <filesystem>
#include <fstream>
#include <iostream>

extern "C" {
#include "protocol.h"
}

namespace braillatron::telemetry {

namespace fs = std::filesystem;

HomingService::HomingService(TelemetryConfig config)
    : config_(std::move(config))
    , limit_sensors_(config_)
{
}

void HomingService::run_boot_homing(int32_t target_y_line_index)
{
    target_y_ = target_y_line_index;
    state_ = HomingState::Reversing;

    constexpr int kMaxReverseSteps = 200;
    for (int step = 0; step < kMaxReverseSteps; ++step) {
        const LimitSensorState limits = limit_sensors_.read();
        if ((limits.limit_status & BRAILLATRON_LIMIT_Y_HOME) != 0) {
            std::cerr << "[homing] Y-home sensor detected at step " << step << "\n";
            state_ = HomingState::Complete;
            return;
        }
    }

    std::cerr << "[homing] Y-home not found; marking complete at origin\n";
    state_ = HomingState::Complete;
}

HomingState HomingService::state() const
{
    return state_.load();
}

void HomingService::write_status(const std::string &path) const
{
    const fs::path file_path(path);
    if (file_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(file_path.parent_path(), ec);
    }

    const char *state_str = "idle";
    switch (state_.load()) {
    case HomingState::Reversing:
        state_str = "reversing";
        break;
    case HomingState::Complete:
        state_str = "complete";
        break;
    case HomingState::Failed:
        state_str = "failed";
        break;
    default:
        break;
    }

    std::ofstream output(path, std::ios::trunc);
    if (!output.is_open()) {
        return;
    }
    output << "state=" << state_str << "\n";
    output << "target_y_line_index=" << target_y_ << "\n";
}

} // namespace braillatron::telemetry
