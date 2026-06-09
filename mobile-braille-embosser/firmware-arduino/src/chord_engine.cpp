/*
 * Temporal chord assembly (V8 spec 2.2, executed on-device).
 *
 * The first dot press while idle opens a 40 ms integration window seeded
 * with the dots currently held. Dot presses during the window are OR'd in.
 * When the window expires the chord is locked and transmitted once as
 * BRAILLATRON_OP_CHORD; the accumulator is then cleared, so released dots
 * can never leak into a later chord.
 *
 * Function keys (D-pad, Enter, Backspace, Shift/TTS, Speech, Menu) bypass
 * the window: any edge transmits the full debounced key state immediately
 * as BRAILLATRON_OP_KEYBOARD_MATRIX.
 */

#include "chord_engine.h"

#include "protocol.h"
#include "protocol_tx.h"

#define CHORD_WINDOW_MS 40u

#define DOT_MASK ((uint16_t)(BRAILLATRON_KEY_DOT_1 | BRAILLATRON_KEY_DOT_2 | \
                             BRAILLATRON_KEY_DOT_3 | BRAILLATRON_KEY_DOT_4 | \
                             BRAILLATRON_KEY_DOT_5 | BRAILLATRON_KEY_DOT_6))

static uint16_t g_previous_state = 0u;
static uint8_t g_chord_accumulator = 0u;
static bool g_window_open = false;
static uint32_t g_window_start_ms = 0u;

void chord_engine_init(void)
{
    g_previous_state = 0u;
    g_chord_accumulator = 0u;
    g_window_open = false;
    g_window_start_ms = 0u;
}

void chord_engine_update(uint16_t key_state, bool state_changed, uint32_t now_ms)
{
    if (state_changed) {
        const uint16_t changed = (uint16_t)(g_previous_state ^ key_state);
        const uint16_t dot_presses =
            (uint16_t)(changed & key_state & DOT_MASK);

        if ((changed & (uint16_t)~DOT_MASK) != 0u) {
            protocol_tx_keyboard_matrix(key_state);
        }

        if (dot_presses != 0u) {
            if (!g_window_open) {
                g_window_open = true;
                g_window_start_ms = now_ms;
                g_chord_accumulator = (uint8_t)(key_state & DOT_MASK);
            } else {
                g_chord_accumulator =
                    (uint8_t)(g_chord_accumulator | (key_state & DOT_MASK));
            }
        }

        g_previous_state = key_state;
    }

    if (g_window_open && (uint32_t)(now_ms - g_window_start_ms) >= CHORD_WINDOW_MS) {
        if (g_chord_accumulator != 0u) {
            protocol_tx_chord(g_chord_accumulator);
        }
        g_chord_accumulator = 0u;
        g_window_open = false;
    }
}
