#pragma once

#include <atomic>

namespace braillatron {

class MotionGate {
public:
    static bool is_blocked();
    static void block(const char *reason);
    /** Explicit operator recover only — never call from a motion path. */
    static void unblock();
    static const char *block_reason();

private:
    static std::atomic<bool> blocked_;
    static const char *reason_;
};

} // namespace braillatron
