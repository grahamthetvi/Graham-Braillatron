#pragma once

#include "emboss_scheduler.h"
#include "kinematics_config.h"
#include "travel_log.h"

#include <cstdint>

namespace braillatron::kinematics {

class MotionController {
public:
    explicit MotionController(KinematicsConfig config);

    void set_row_handlers(RowStrikeHandler row_a_handler, RowStrikeHandler row_b_handler);

    void reset_position(int64_t position_microsteps = 0);
    void emboss(uint8_t dot_mask);

    void log_carriage_microsteps(int32_t delta_microsteps);
    void log_carriage_full_steps(int32_t delta_full_steps);
    void advance_line_10mm();

    TravelLog &travel_log();
    const TravelLog &travel_log() const;
    EmbossScheduler &scheduler();
    uint32_t row_b_deferral_microsteps() const;

private:
    KinematicsConfig config_;
    TravelLog travel_log_;
    EmbossScheduler scheduler_;
};

} // namespace braillatron::kinematics
