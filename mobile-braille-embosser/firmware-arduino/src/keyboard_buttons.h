#pragma once

#include <stdbool.h>
#include <stdint.h>

void keyboard_buttons_init(void);

/*
 * Samples at most once per millisecond (call freely from the main loop).
 * Returns true when the debounced key state changed on this tick.
 */
bool keyboard_buttons_poll(uint32_t now_ms);

/* Debounced state in logical BRAILLATRON_KEY_* bit positions, 1 = pressed. */
uint16_t keyboard_buttons_state(void);
