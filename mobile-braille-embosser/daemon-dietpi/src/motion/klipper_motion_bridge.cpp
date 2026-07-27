#include "klipper_motion_bridge.h"

#include "../kinematics/motion_constants.h"
#include "../motion_gate.h"

#include <cmath>
#include <iostream>

namespace braillatron::motion {

KlipperMotionBridge::KlipperMotionBridge(KlipperConfig config, MotionService &motion)
    : config_(std::move(config))
    , motion_(motion)
    , client_(config_)
{
}

bool KlipperMotionBridge::connect()
{
    ready_ = client_.ping();
    if (ready_) {
        std::cerr << "[klipper] Moonraker reachable at " << config_.moonraker_url << "\n";
        attach_row_strike_handlers();
    } else if (config_.enabled) {
        std::cerr << "[klipper] Moonraker unavailable at " << config_.moonraker_url
                  << " — kinematics-only motion continues\n";
    }
    return ready_;
}

void KlipperMotionBridge::attach_row_strike_handlers()
{
    motion_.set_row_strike_log([this](uint8_t pin_mask, int64_t travel) {
        on_row_strike(pin_mask, travel);
    });
}

void KlipperMotionBridge::on_row_strike(uint8_t pin_mask, int64_t absolute_microsteps)
{
    if (!ready_ || MotionGate::is_blocked()) {
        return;
    }

    // EmbossScheduler passes absolute travel-log position, not a relative delta.
    if (!have_last_x_) {
        last_x_microsteps_ = absolute_microsteps;
        have_last_x_ = true;
    } else {
        const int64_t delta = absolute_microsteps - last_x_microsteps_;
        last_x_microsteps_ = absolute_microsteps;
        const double mm = braillatron::kinematics::microsteps_to_mm(delta);
        if (std::abs(mm) >= 0.001) {
            client_.move_x_relative_mm(mm, config_.x_move_speed_mm_s);
        }
    }

    for (unsigned bit = 0; bit < 6; ++bit) {
        if ((pin_mask & (1u << bit)) == 0) {
            continue;
        }
        const std::string stepper = config_.emboss_stepper_name(bit + 1);
        if (!stepper.empty()) {
            client_.stepper_buzz(stepper, config_.stepper_buzz_duration_ms);
        }
    }
}

bool KlipperMotionBridge::feed_lines(int32_t delta)
{
    if (!ready_ || MotionGate::is_blocked() || delta == 0) {
        return false;
    }

    const double mm = config_.y_feed_mm_per_line * static_cast<double>(delta);
    return client_.feed_y_mm(mm, config_.y_feed_speed_mm_s);
}

bool KlipperMotionBridge::home_y()
{
    if (!ready_ || MotionGate::is_blocked()) {
        return false;
    }
    return client_.home_y();
}

bool KlipperMotionBridge::emergency_stop()
{
    if (!config_.enabled) {
        return false;
    }
    return client_.emergency_stop();
}

bool KlipperMotionBridge::paper_edge_active() const
{
    if (!ready_) {
        return false;
    }
    return client_.query_endstops().paper_edge;
}

} // namespace braillatron::motion
