#include "motion_gate.h"

namespace braillatron {

std::atomic<bool> MotionGate::blocked_ {false};
const char *MotionGate::reason_ = nullptr;
std::atomic<int> MotionGate::arduino_clear_pulses_ {0};

bool MotionGate::is_blocked()
{
    return blocked_.load();
}

void MotionGate::block(const char *reason)
{
    reason_ = reason;
    blocked_.store(true);
}

void MotionGate::unblock()
{
    reason_ = nullptr;
    blocked_.store(false);
}

const char *MotionGate::block_reason()
{
    return reason_;
}

void MotionGate::request_arduino_clear()
{
    /* ~3 s of 500 ms telemetry relays so a dropped frame still lands. */
    arduino_clear_pulses_.store(6);
}

bool MotionGate::consume_arduino_clear_pulse()
{
    int remaining = arduino_clear_pulses_.load();
    while (remaining > 0) {
        if (arduino_clear_pulses_.compare_exchange_weak(remaining, remaining - 1)) {
            return true;
        }
    }
    return false;
}

} // namespace braillatron
