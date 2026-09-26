#include "homing_service.h"

#include "../motion/moonraker_client.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace braillatron::telemetry {

namespace fs = std::filesystem;

HomingService::HomingService(TelemetryConfig config)
    : config_(std::move(config))
{
}

void HomingService::set_moonraker_client(motion::MoonrakerClient *client)
{
    moonraker_ = client;
}

void HomingService::run_boot_homing(int32_t target_y_line_index)
{
    target_y_ = target_y_line_index;
    fail_reason_.clear();
    state_ = HomingState::Reversing;

    // V5.1 Y home is Klipper Y-STOP ^PA15 via Moonraker. gpio_y_home in
    // telemetry.conf is empty on purpose; there is no Pi GPIO fallback.
    if (moonraker_ == nullptr || !moonraker_->ping()) {
        std::cerr << "[homing] Moonraker unavailable; Y home requires Klipper "
                     "endstops (Y-STOP PA15). GPIO fallback is unused on V5.1.\n";
        fail_reason_ = "moonraker_unavailable";
        state_ = HomingState::Failed;
        return;
    }

    std::cerr << "[homing] issuing Klipper G28 Y via Moonraker\n";
    if (!moonraker_->home_y()) {
        std::cerr << "[homing] Klipper G28 Y failed; not treating Y as homed\n";
        fail_reason_ = "g28_y_failed";
        state_ = HomingState::Failed;
        return;
    }

    // V9 §3.3: after home (Y=0), fast-forward to the saved line index.
    if (target_y_ > 0) {
        constexpr double kMmPerLine = 10.0;
        constexpr double kFeedSpeedMmS = 20.0;
        const double mm = kMmPerLine * static_cast<double>(target_y_);
        std::cerr << "[homing] restoring paper to y_line_index=" << target_y_
                  << " (" << mm << " mm)\n";
        if (!moonraker_->feed_y_mm(mm, kFeedSpeedMmS)) {
            std::cerr << "[homing] warning: restore feed to saved line failed\n";
        }
    }

    state_ = HomingState::Complete;
}

HomingState HomingService::state() const
{
    return state_.load();
}

void HomingService::write_status(const std::string &path) const
{
    const fs::path file_path(path.empty() ? config_.homing_status_path : path);
    if (file_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(file_path.parent_path(), ec);
    }

    const HomingState current = state_.load();
    const char *state_str = "idle";
    switch (current) {
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

    std::ofstream output(file_path, std::ios::trunc);
    if (!output.is_open()) {
        return;
    }
    output << "state=" << state_str << "\n";
    output << "target_y_line_index=" << target_y_ << "\n";
    if (current == HomingState::Failed && !fail_reason_.empty()) {
        output << "reason=" << fail_reason_ << "\n";
    }
}

} // namespace braillatron::telemetry
