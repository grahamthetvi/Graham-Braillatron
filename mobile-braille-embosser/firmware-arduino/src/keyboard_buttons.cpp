/*
 * Direct-pin keyboard reader (V5.1 topology).
 *
 * Non-blocking integrator debounce: each key has a counter that charges
 * toward DEBOUNCE_MS while the raw pin reads pressed and discharges toward 0
 * while released. The debounced state flips only at the rails, giving a
 * 15 ms threshold tailored for Cherry MX switches (V5.1 Part 4.1) without
 * delay() or ISR work that could starve the MPU6050 freefall interrupt.
 */

#include "keyboard_buttons.h"

#include "pins.h"

#include <Arduino.h>

#define DEBOUNCE_MS 15u

static uint8_t g_integrator[BUTTON_COUNT];
static uint16_t g_debounced_state = 0u;
static uint32_t g_last_sample_ms = 0u;

void keyboard_buttons_init(void)
{
    for (uint8_t i = 0u; i < BUTTON_COUNT; ++i) {
        pinMode(BUTTON_PINS[i], INPUT_PULLUP);
        g_integrator[i] = 0u;
    }

    g_debounced_state = 0u;
    g_last_sample_ms = millis();
}

bool keyboard_buttons_poll(uint32_t now_ms)
{
    if (now_ms == g_last_sample_ms) {
        return false;
    }
    g_last_sample_ms = now_ms;

    uint16_t state = g_debounced_state;

    for (uint8_t i = 0u; i < BUTTON_COUNT; ++i) {
        const bool raw_pressed = digitalRead(BUTTON_PINS[i]) == LOW;
        const uint16_t bit = BUTTON_KEY_BITS[i];

        if (raw_pressed) {
            if (g_integrator[i] < DEBOUNCE_MS) {
                ++g_integrator[i];
                if (g_integrator[i] == DEBOUNCE_MS) {
                    state |= bit;
                }
            }
        } else {
            if (g_integrator[i] > 0u) {
                --g_integrator[i];
                if (g_integrator[i] == 0u) {
                    state = (uint16_t)(state & ~bit);
                }
            }
        }
    }

    if (state == g_debounced_state) {
        return false;
    }

    g_debounced_state = state;
    return true;
}

uint16_t keyboard_buttons_state(void)
{
    return g_debounced_state;
}
