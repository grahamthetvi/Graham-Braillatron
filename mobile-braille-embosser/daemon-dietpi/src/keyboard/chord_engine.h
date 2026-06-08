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

std::optional<char> braille_dots_to_char(uint8_t dot_mask);

class ChordEngine {
public:
    explicit ChordEngine(uint32_t window_ms);

    void on_matrix_state(uint16_t key_state, uint64_t now_ms);
    std::optional<char> poll(uint64_t now_ms);
    std::optional<ControlEdge> poll_control_edge();

private:
    uint32_t window_ms_;
    uint16_t current_state_ = 0;
    uint8_t chord_accumulator_ = 0;
    uint64_t last_dot_activity_ms_ = 0;
    bool chord_pending_ = false;
    std::vector<ControlEdge> pending_controls_;
};

} // namespace braillatron::keyboard
