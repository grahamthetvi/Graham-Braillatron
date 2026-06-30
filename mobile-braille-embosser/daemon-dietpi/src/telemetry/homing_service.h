#pragma once

#include "limit_sensors.h"
#include "telemetry_config.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace braillatron::motion {
class MoonrakerClient;
}

namespace braillatron::telemetry {

enum class HomingState {
    Idle,
    Reversing,
    Complete,
    Failed,
};

class HomingService {
public:
    explicit HomingService(TelemetryConfig config);

    void set_moonraker_client(motion::MoonrakerClient *client);

    void run_boot_homing(int32_t target_y_line_index);
    HomingState state() const;
    void write_status(const std::string &path) const;

private:
    TelemetryConfig config_;
    LimitSensors limit_sensors_;
    motion::MoonrakerClient *moonraker_ = nullptr;
    std::atomic<HomingState> state_ {HomingState::Idle};
    int32_t target_y_ = 0;
};

} // namespace braillatron::telemetry
