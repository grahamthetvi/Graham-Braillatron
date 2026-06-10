#include "host_chord_assembler.h"

extern "C" {
#include "protocol.h"
}

namespace braillatron::keyboard {

namespace {

constexpr uint64_t kChordWindowMs = 40;

constexpr uint16_t kDotMask = static_cast<uint16_t>(
    BRAILLATRON_KEY_DOT_1 | BRAILLATRON_KEY_DOT_2 | BRAILLATRON_KEY_DOT_3 |
    BRAILLATRON_KEY_DOT_4 | BRAILLATRON_KEY_DOT_5 | BRAILLATRON_KEY_DOT_6);

} // namespace

void HostChordAssembler::set_keyboard_matrix_handler(KeyboardMatrixHandler handler)
{
    keyboard_matrix_handler_ = std::move(handler);
}

void HostChordAssembler::set_chord_handler(ChordHandler handler)
{
    chord_handler_ = std::move(handler);
}

void HostChordAssembler::reset()
{
    previous_state_ = 0;
    chord_accumulator_ = 0;
    window_open_ = false;
    window_start_ms_ = 0;
}

void HostChordAssembler::update(uint16_t key_state, bool state_changed, uint64_t now_ms)
{
    if (state_changed) {
        const uint16_t changed = static_cast<uint16_t>(previous_state_ ^ key_state);
        const uint16_t dot_presses = static_cast<uint16_t>(changed & key_state & kDotMask);

        if ((changed & static_cast<uint16_t>(~kDotMask)) != 0u && keyboard_matrix_handler_) {
            keyboard_matrix_handler_(key_state);
        }

        if (dot_presses != 0u) {
            if (!window_open_) {
                window_open_ = true;
                window_start_ms_ = now_ms;
                chord_accumulator_ = static_cast<uint8_t>(key_state & kDotMask);
            } else {
                chord_accumulator_ =
                    static_cast<uint8_t>(chord_accumulator_ | (key_state & kDotMask));
            }
        }

        previous_state_ = key_state;
    }

    if (window_open_ && now_ms - window_start_ms_ >= kChordWindowMs) {
        if (chord_accumulator_ != 0u && chord_handler_) {
            chord_handler_(chord_accumulator_);
        }
        chord_accumulator_ = 0;
        window_open_ = false;
    }
}

} // namespace braillatron::keyboard
