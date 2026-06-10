#pragma once

#include <cstdint>
#include <optional>
#include <vector>

extern "C" {
#include "protocol.h"
}

namespace braillatron::keyboard {

constexpr uint16_t BRAILLE_DOT_MASK =
    BRAILLATRON_KEY_DOT_1 | BRAILLATRON_KEY_DOT_2 | BRAILLATRON_KEY_DOT_3 |
    BRAILLATRON_KEY_DOT_4 | BRAILLATRON_KEY_DOT_5 | BRAILLATRON_KEY_DOT_6;

constexpr uint16_t CONTROL_KEY_MASK =
    BRAILLATRON_KEY_DPAD_UP | BRAILLATRON_KEY_DPAD_DOWN | BRAILLATRON_KEY_BACKSPACE |
    BRAILLATRON_KEY_ENTER | BRAILLATRON_KEY_SHIFT_TTS | BRAILLATRON_KEY_SPEECH |
    BRAILLATRON_KEY_MENU;

enum class ControlKey : uint8_t {
    DpadUp,
    DpadDown,
    Backspace,
    Enter,
    ShiftTts,
    Speech,
    Menu,
};

struct ControlEdge {
    ControlKey key;
    bool pressed;
};

#include <string>

std::optional<std::string> braille_dots_to_string(uint8_t dot_mask);
/*
 * Chord assembly (40 ms integration window) now runs on the Arduino, which
 * delivers locked chords via BRAILLATRON_OP_CHORD. This class only extracts
 * control-key edges from BRAILLATRON_OP_KEYBOARD_MATRIX state frames.
 */
class ChordEngine {
public:
    ChordEngine() = default;

    void on_key_state(uint16_t key_state);
    std::optional<ControlEdge> poll_control_edge();

private:
    uint16_t current_state_ = 0;
    std::vector<ControlEdge> pending_controls_;
};

} // namespace braillatron::keyboard
