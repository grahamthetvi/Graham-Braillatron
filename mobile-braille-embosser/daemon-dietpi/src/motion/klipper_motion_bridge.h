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
    void on_row_strike(uint8_t pin_mask, int64_t travel_microsteps);

    KlipperConfig config_;
    MotionService &motion_;
    MoonrakerClient client_;
    bool ready_ = false;
};

} // namespace braillatron::motion
