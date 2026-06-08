#pragma once

#include "kinematics_config.h"
#include "linkage_model.h"
#include "motion_constants.h"
#include "spatial_delay_line.h"
#include "travel_log.h"

#include <cstdint>
#include <functional>

namespace braillatron::kinematics {

using RowStrikeHandler = std::function<void(uint8_t pin_mask, int64_t travel_microsteps)>;

class EmbossScheduler {
public:
    EmbossScheduler(KinematicsConfig config, TravelLog &travel_log, RowStrikeHandler row_a_handler,
                    RowStrikeHandler row_b_handler);

    void submit_dot_matrix(uint8_t dot_mask);
    void on_travel_logged(int32_t delta_microsteps);
    void flush_due_row_b();
    void set_row_handlers(RowStrikeHandler row_a_handler, RowStrikeHandler row_b_handler);

    uint32_t row_b_deferral_microsteps() const;

private:
    KinematicsConfig config_;
    TravelLog &travel_log_;
    LinkageModel linkage_;
    SpatialDelayLine delay_line_;
    RowStrikeHandler row_a_handler_;
    RowStrikeHandler row_b_handler_;

    void schedule_row_b(int64_t row_a_strike_microsteps, uint8_t row_b_mask);
};

} // namespace braillatron::kinematics
