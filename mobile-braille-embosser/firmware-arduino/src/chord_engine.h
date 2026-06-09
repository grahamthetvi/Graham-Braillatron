#pragma once

#include <stdbool.h>
#include <stdint.h>

void chord_engine_init(void);

/*
 * Feed the debounced key state every tick. state_changed must be true when
 * key_state differs from the previous tick. Emits BRAILLATRON_OP_CHORD when
 * the 40 ms integration window expires and BRAILLATRON_OP_KEYBOARD_MATRIX on
 * function-key edges.
 */
void chord_engine_update(uint16_t key_state, bool state_changed, uint32_t now_ms);
