#pragma once

/*
 * PROFILE: skeleton_v4 — keep in sync with daemon-dietpi/config/matrix_map.conf.
 * Default path: firmware sends physical matrix bits; Pi applies matrix_map.conf.
 * Call matrix_map_remap() here only if the device must emit logical keys on-wire.
 */

#include <stdint.h>

extern "C" {
#include "protocol.h"
}

static const uint16_t MATRIX_TO_LOGICAL[16] = {
    BRAILLATRON_KEY_DOT_1,
    BRAILLATRON_KEY_DOT_2,
    BRAILLATRON_KEY_DOT_3,
    BRAILLATRON_KEY_DOT_4,
    BRAILLATRON_KEY_DOT_5,
    BRAILLATRON_KEY_DOT_6,
    BRAILLATRON_KEY_DPAD_UP,
    BRAILLATRON_KEY_DPAD_DOWN,
    BRAILLATRON_KEY_BACKSPACE,
    BRAILLATRON_KEY_ENTER,
    BRAILLATRON_KEY_SHIFT_TTS,
    BRAILLATRON_KEY_SPEECH,
    BRAILLATRON_KEY_MENU,
    0u,
    0u,
    0u,
};

static inline uint16_t matrix_map_remap(uint16_t physical_state)
{
    uint16_t logical = 0u;

    for (uint8_t bit = 0u; bit < 16u; ++bit) {
        if ((physical_state & (uint16_t)(1u << bit)) != 0u) {
            logical |= MATRIX_TO_LOGICAL[bit];
        }
    }

    return logical;
}
