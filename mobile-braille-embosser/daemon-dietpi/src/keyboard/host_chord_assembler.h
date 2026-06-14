#pragma once

#include <cstdint>
#include <functional>

namespace braillatron::keyboard {

/*
 * Host-side chord assembly for evdev bench input.
 * Control-key edges invoke keyboard_matrix_handler immediately; dot chords
 * accumulate while any dot is held and commit when all dot keys are released.
 */
class HostChordAssembler {
public:
    using KeyboardMatrixHandler = std::function<void(uint16_t key_state)>;
    using ChordHandler = std::function<void(uint8_t dot_mask)>;

    void set_keyboard_matrix_handler(KeyboardMatrixHandler handler);
    void set_chord_handler(ChordHandler handler);

    void reset();
    void update(uint16_t key_state, bool state_changed, uint64_t now_ms);

private:
    void commit_chord();
    void maybe_commit_on_release(uint16_t key_state);

    KeyboardMatrixHandler keyboard_matrix_handler_;
    ChordHandler chord_handler_;

    uint16_t previous_state_ = 0;
    uint8_t chord_accumulator_ = 0;
    bool chord_active_ = false;
    uint64_t chord_start_ms_ = 0;
};

} // namespace braillatron::keyboard
