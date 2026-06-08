#include "motion_gate.h"

namespace braillatron {

std::atomic<bool> MotionGate::blocked_ {false};
const char *MotionGate::reason_ = nullptr;

bool MotionGate::is_blocked()
{
    return blocked_.load();
}

void MotionGate::block(const char *reason)
{
    reason_ = reason;
    blocked_.store(true);
}

const char *MotionGate::block_reason()
{
    return reason_;
}

} // namespace braillatron
