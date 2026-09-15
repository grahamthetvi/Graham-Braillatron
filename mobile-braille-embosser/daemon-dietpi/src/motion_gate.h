#pragma once

#include <atomic>

namespace braillatron {

class MotionGate {
public:
    static bool is_blocked();
    static void block(const char *reason);
    static void unblock();
    static const char *block_reason();

    /** Pulse BRAILLATRON_LIMIT_CLEAR_FREEFALL on the next telemetry frames. */
    static void request_arduino_clear();
    static bool consume_arduino_clear_pulse();

private:
    static std::atomic<bool> blocked_;
    static const char *reason_;
    static std::atomic<int> arduino_clear_pulses_;
};

} // namespace braillatron
