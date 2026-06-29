#pragma once

#include "../../connect/connect_client.h"
#include "../../connect/json_utils.h"
#include "../../keyboard/global_hooks.h"

#include <cstdint>

namespace braillatron::ui {

constexpr uint8_t kSkipBackDotMask = 0x07;    // dots 1-2-3 (fds)
constexpr uint8_t kSkipForwardDotMask = 0x38; // dots 4-5-6 (jkl)

inline bool is_skip_chord(uint8_t dot_mask)
{
    return dot_mask == kSkipBackDotMask || dot_mask == kSkipForwardDotMask;
}

class HeldAudioSkip {
public:
    void reset()
    {
        last_skip_ms_ = 0;
        skip_forward_held_ = false;
        skip_back_held_ = false;
    }

    void poll(uint64_t now_ms, braillatron::connect::ConnectClient *connect)
    {
        if (connect == nullptr) {
            return;
        }

        const uint8_t held = braillatron::hooks::held_dot_mask();
        const bool forward = (held & kSkipForwardDotMask) == kSkipForwardDotMask;
        const bool back = (held & kSkipBackDotMask) == kSkipBackDotMask;
        if (!forward && !back) {
            skip_forward_held_ = false;
            skip_back_held_ = false;
            return;
        }
        if (forward && back) {
            return;
        }

        const bool first_hold = forward ? !skip_forward_held_ : !skip_back_held_;
        if (forward) {
            skip_forward_held_ = true;
        } else {
            skip_back_held_ = true;
        }

        constexpr uint64_t kInitialDelayMs = 400;
        constexpr uint64_t kRepeatMs = 500;
        const uint64_t delay = first_hold ? kInitialDelayMs : kRepeatMs;
        if (last_skip_ms_ != 0 && now_ms - last_skip_ms_ < delay) {
            return;
        }

        const char *cmd = forward ? "music.skip_forward" : "music.skip_backward";
        if (braillatron::connect::json_get_bool(connect->request(cmd), "ok", false)) {
            last_skip_ms_ = now_ms;
        }
    }

private:
    uint64_t last_skip_ms_ = 0;
    bool skip_forward_held_ = false;
    bool skip_back_held_ = false;
};

} // namespace braillatron::ui
