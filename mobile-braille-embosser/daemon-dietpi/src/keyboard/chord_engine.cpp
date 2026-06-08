#include "chord_engine.h"

extern "C" {
#include "protocol.h"
}

#include <array>
#include <vector>

namespace braillatron::keyboard {

namespace {

constexpr uint16_t kControlBits[] = {
    BRAILLATRON_KEY_DPAD_UP,
    BRAILLATRON_KEY_DPAD_DOWN,
    BRAILLATRON_KEY_BACKSPACE,
    BRAILLATRON_KEY_ENTER,
    BRAILLATRON_KEY_SHIFT_TTS,
    BRAILLATRON_KEY_SPEECH,
    BRAILLATRON_KEY_MENU,
};

constexpr ControlKey kControlKeys[] = {
    ControlKey::DpadUp,
    ControlKey::DpadDown,
    ControlKey::Backspace,
    ControlKey::Enter,
    ControlKey::ShiftTts,
    ControlKey::Speech,
    ControlKey::Menu,
};

static_assert(sizeof(kControlBits) / sizeof(kControlBits[0]) ==
                  sizeof(kControlKeys) / sizeof(kControlKeys[0]),
              "control key tables must match");

struct BrailleEntry {
    uint8_t dots;
    char character;
};

constexpr std::array<BrailleEntry, 26> kBrailleAlphabet = {{
    {0x01, 'a'}, {0x03, 'b'}, {0x09, 'c'}, {0x19, 'd'}, {0x11, 'e'}, {0x0B, 'f'},
    {0x1B, 'g'}, {0x13, 'h'}, {0x0A, 'i'}, {0x1A, 'j'}, {0x05, 'k'}, {0x07, 'l'},
    {0x0D, 'm'}, {0x1D, 'n'}, {0x15, 'o'}, {0x17, 'p'}, {0x1F, 'q'}, {0x17, 'r'},
    {0x0E, 's'}, {0x1E, 't'}, {0x25, 'u'}, {0x27, 'v'}, {0x3A, 'w'}, {0x2D, 'x'},
    {0x3D, 'y'}, {0x35, 'z'},
}};

} // namespace

std::optional<char> braille_dots_to_char(uint8_t dot_mask)
{
    for (const BrailleEntry &entry : kBrailleAlphabet) {
        if (entry.dots == dot_mask) {
            return entry.character;
        }
    }
    return std::nullopt;
}

ChordEngine::ChordEngine(uint32_t window_ms)
    : window_ms_(window_ms)
{
}

void ChordEngine::on_matrix_state(uint16_t key_state, uint64_t now_ms)
{
    const uint16_t previous = current_state_;
    current_state_ = key_state;

    const uint16_t changed = static_cast<uint16_t>(previous ^ key_state);
    if (changed == 0) {
        return;
    }

    for (size_t i = 0; i < sizeof(kControlBits) / sizeof(kControlBits[0]); ++i) {
        const uint16_t bit = kControlBits[i];
        if ((changed & bit) == 0) {
            continue;
        }

        pending_controls_.push_back(ControlEdge {
            kControlKeys[i],
            (key_state & bit) != 0,
        });
    }

    const uint16_t dot_changes = static_cast<uint16_t>(changed & BRAILLE_DOT_MASK);
    if (dot_changes != 0) {
        chord_accumulator_ =
            static_cast<uint8_t>(chord_accumulator_ | (key_state & BRAILLE_DOT_MASK));
        last_dot_activity_ms_ = now_ms;
        chord_pending_ = chord_accumulator_ != 0;
    }
}

std::optional<char> ChordEngine::poll(uint64_t now_ms)
{
    if (!chord_pending_) {
        return std::nullopt;
    }

    if (now_ms - last_dot_activity_ms_ < window_ms_) {
        return std::nullopt;
    }

    const uint8_t dots = chord_accumulator_;
    chord_accumulator_ = 0;
    chord_pending_ = false;

    return braille_dots_to_char(dots);
}

std::optional<ControlEdge> ChordEngine::poll_control_edge()
{
    if (pending_controls_.empty()) {
        return std::nullopt;
    }

    ControlEdge edge = pending_controls_.front();
    pending_controls_.erase(pending_controls_.begin());
    return edge;
}

} // namespace braillatron::keyboard
