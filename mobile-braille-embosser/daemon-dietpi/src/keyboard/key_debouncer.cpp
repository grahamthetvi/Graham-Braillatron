#include "key_debouncer.h"

namespace braillatron::keyboard {

void KeyDebouncer::set_raw_state(uint16_t raw_state)
{
    raw_state_ = raw_state;
}

bool KeyDebouncer::poll(uint64_t now_ms, uint16_t *debounced_state)
{
    if (debounced_state == nullptr) {
        return false;
    }

    if (last_poll_ms_ == 0) {
        last_poll_ms_ = now_ms;
    }

    const uint16_t previous = debounced_state_;
    const uint64_t elapsed = now_ms - last_poll_ms_;
    last_poll_ms_ = now_ms;

    if (elapsed == 0) {
        *debounced_state = debounced_state_;
        return false;
    }

    for (size_t bit = 0; bit < kKeyBits; ++bit) {
        const uint16_t mask = static_cast<uint16_t>(1u << bit);
        const bool raw_pressed = (raw_state_ & mask) != 0;
        const bool debounced_pressed = (debounced_state_ & mask) != 0;

        if (raw_pressed && !debounced_pressed) {
            integrator_[bit] += static_cast<unsigned>(elapsed);
            if (integrator_[bit] >= kDebounceMs) {
                debounced_state_ = static_cast<uint16_t>(debounced_state_ | mask);
                integrator_[bit] = kDebounceMs;
            }
        } else if (!raw_pressed && debounced_pressed) {
            if (integrator_[bit] <= static_cast<unsigned>(elapsed)) {
                integrator_[bit] = 0;
            } else {
                integrator_[bit] -= static_cast<unsigned>(elapsed);
            }
            if (integrator_[bit] == 0) {
                debounced_state_ = static_cast<uint16_t>(debounced_state_ & ~mask);
            }
        } else if (raw_pressed) {
            integrator_[bit] = kDebounceMs;
        } else {
            integrator_[bit] = 0;
        }
    }

    *debounced_state = debounced_state_;
    return previous != debounced_state_;
}

} // namespace braillatron::keyboard
