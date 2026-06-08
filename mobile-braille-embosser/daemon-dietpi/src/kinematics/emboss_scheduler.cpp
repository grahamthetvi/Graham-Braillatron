#include "emboss_scheduler.h"

#include <utility>
#include <vector>

namespace braillatron::kinematics {

EmbossScheduler::EmbossScheduler(KinematicsConfig config, TravelLog &travel_log,
                                   RowStrikeHandler row_a_handler, RowStrikeHandler row_b_handler)
    : config_(std::move(config))
    , travel_log_(travel_log)
    , linkage_(config_)
    , delay_line_(config_.spatial_delay_line_capacity)
    , row_a_handler_(std::move(row_a_handler))
    , row_b_handler_(std::move(row_b_handler))
{
}

void EmbossScheduler::submit_dot_matrix(uint8_t dot_mask)
{
    const uint8_t masked = static_cast<uint8_t>(dot_mask & BRAILLE_DOT_MASK);
    if (masked == 0) {
        return;
    }

    const int64_t strike_position = travel_log_.position_microsteps();
    const uint8_t row_a = row_a_pins(masked);
    const uint8_t row_b = row_b_pins(masked);

    if (row_a != 0 && row_a_handler_) {
        row_a_handler_(row_a, strike_position);
    }

    if (row_b != 0) {
        schedule_row_b(strike_position, row_b);
    }
}

void EmbossScheduler::on_travel_logged(int32_t delta_microsteps)
{
    travel_log_.log_microsteps(delta_microsteps);
    flush_due_row_b();
}

void EmbossScheduler::flush_due_row_b()
{
    const int64_t position = travel_log_.position_microsteps();
    const std::vector<DeferredRowBStrike> due = delay_line_.drain_due(position);

    for (const DeferredRowBStrike &strike : due) {
        if (row_b_handler_) {
            row_b_handler_(strike.row_b_pin_mask, strike.fire_at_microsteps);
        }
    }
}

void EmbossScheduler::set_row_handlers(RowStrikeHandler row_a_handler,
                                       RowStrikeHandler row_b_handler)
{
    row_a_handler_ = std::move(row_a_handler);
    row_b_handler_ = std::move(row_b_handler);
}

uint32_t EmbossScheduler::row_b_deferral_microsteps() const
{
    return MICROSTEPS_ROW_B_OFFSET + linkage_.tdc_dwell_microsteps();
}

void EmbossScheduler::schedule_row_b(int64_t row_a_strike_microsteps, uint8_t row_b_mask)
{
    DeferredRowBStrike entry {};
    entry.row_a_strike_microsteps = row_a_strike_microsteps;
    entry.fire_at_microsteps = row_a_strike_microsteps + row_b_deferral_microsteps();
    entry.row_b_pin_mask = row_b_mask;
    delay_line_.enqueue(entry);
}

} // namespace braillatron::kinematics
