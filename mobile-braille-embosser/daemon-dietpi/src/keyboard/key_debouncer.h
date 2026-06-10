#pragma once

#include <array>
#include <cstdint>

namespace braillatron::keyboard {

/*
 * Per-key 15 ms integrator debounce (mirrors firmware keyboard_buttons logic).
 */
class KeyDebouncer {
public:
    static constexpr unsigned kDebounceMs = 15;
    static constexpr size_t kKeyBits = 16;

    void set_raw_state(uint16_t raw_state);
    bool poll(uint64_t now_ms, uint16_t *debounced_state);

private:
    uint16_t raw_state_ = 0;
    uint16_t debounced_state_ = 0;
    std::array<unsigned, kKeyBits> integrator_ {};
    uint64_t last_poll_ms_ = 0;
};

} // namespace braillatron::keyboard
