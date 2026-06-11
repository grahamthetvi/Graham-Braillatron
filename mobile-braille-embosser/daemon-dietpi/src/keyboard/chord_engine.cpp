#include "chord_engine.h"

#include "../documents/liblouis_bridge.h"

extern "C" {
#include "protocol.h"
}

#include <array>

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

} // namespace

std::optional<std::string> braille_dots_to_string(uint8_t dot_mask)
{
    const std::string result =
        documents::translate_backward_dots(dot_mask, documents::BrailleTable::UebG2);
    if (result.empty() && dot_mask != 0) {
        return std::nullopt;
    }
    return result;
}

void ChordEngine::on_key_state(uint16_t key_state)
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
