#pragma once

#include "klipper_config.h"
#include "moonraker_client.h"
#include "motion_service.h"

#include <memory>
#include <string>

namespace braillatron::motion {

class KlipperMotionBridge {
public:
    KlipperMotionBridge(KlipperConfig config, MotionService &motion);

    bool connect();
    bool is_ready() const { return ready_; }
    MoonrakerClient &client() { return client_; }
    const MoonrakerClient &client() const { return client_; }

    void attach_row_strike_handlers();
    bool feed_lines(int32_t delta);
    bool home_y();
    bool emergency_stop();
    bool paper_edge_active() const;

private:
    // Scheduler passes absolute carriage position at strike time; convert to
    // relative Moonraker moves from the last commanded absolute position.
    void on_row_strike(uint8_t pin_mask, int64_t absolute_microsteps);

    KlipperConfig config_;
    MotionService &motion_;
    MoonrakerClient client_;
    bool ready_ = false;
    int64_t last_x_microsteps_ = 0;
    bool have_last_x_ = false;
};

} // namespace braillatron::motion
