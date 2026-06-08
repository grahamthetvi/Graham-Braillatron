#include "motion_controller.h"

#include "motion_gate.h"

namespace braillatron::kinematics {

MotionController::MotionController(KinematicsConfig config)
    : config_(std::move(config))
    , scheduler_(config_, travel_log_, nullptr, nullptr)
{
}

void MotionController::set_row_handlers(RowStrikeHandler row_a_handler,
                                        RowStrikeHandler row_b_handler)
{
    scheduler_.set_row_handlers(std::move(row_a_handler), std::move(row_b_handler));
}

void MotionController::reset_position(int64_t position_microsteps)
{
    travel_log_.reset(position_microsteps);
}

void MotionController::emboss(uint8_t dot_mask)
{
    if (braillatron::MotionGate::is_blocked()) {
        return;
    }

    scheduler_.submit_dot_matrix(dot_mask);
}

void MotionController::log_carriage_microsteps(int32_t delta_microsteps)
{
    if (braillatron::MotionGate::is_blocked()) {
        return;
    }

    scheduler_.on_travel_logged(delta_microsteps);
}

void MotionController::log_carriage_full_steps(int32_t delta_full_steps)
{
    log_carriage_microsteps(delta_full_steps * static_cast<int32_t>(MICROSTEPS_PER_FULL_STEP));
}

void MotionController::advance_line_10mm()
{
    log_carriage_microsteps(static_cast<int32_t>(MICROSTEPS_PER_10MM_LINE));
}

TravelLog &MotionController::travel_log()
{
    return travel_log_;
}

const TravelLog &MotionController::travel_log() const
{
    return travel_log_;
}

EmbossScheduler &MotionController::scheduler()
{
    return scheduler_;
}

uint32_t MotionController::row_b_deferral_microsteps() const
{
    return scheduler_.row_b_deferral_microsteps();
}

} // namespace braillatron::kinematics
