#include "keyboard_matrix.h"

#include "pins.h"
#include "protocol_tx.h"

#include <Arduino.h>
#include <avr/interrupt.h>
#include <avr/io.h>

#define DEBOUNCE_WINDOW_BITS  8u
#define MATRIX_KEY_COUNT      16u

static uint8_t g_debounce_history[MATRIX_KEY_COUNT];
static volatile uint16_t g_debounced_state = 0u;
static volatile bool g_matrix_changed = false;

static uint8_t read_column(uint8_t col)
{
    return (uint8_t)digitalRead(MATRIX_COL_PINS[col]);
}

static void drive_rows_idle(void)
{
    for (uint8_t row = 0u; row < 4u; ++row) {
        digitalWrite(MATRIX_ROW_PINS[row], HIGH);
    }
}

static void strobe_row_low(uint8_t active_row)
{
    for (uint8_t row = 0u; row < 4u; ++row) {
        digitalWrite(MATRIX_ROW_PINS[row], row == active_row ? LOW : HIGH);
    }
}

static uint16_t scan_raw_matrix(void)
{
    uint16_t raw = 0u;

    for (uint8_t row = 0u; row < 4u; ++row) {
        strobe_row_low(row);
        /* Allow column lines to settle after row transition. */
        __asm__ __volatile__("nop");
        __asm__ __volatile__("nop");
        __asm__ __volatile__("nop");
        __asm__ __volatile__("nop");

        for (uint8_t col = 0u; col < 4u; ++col) {
            if (read_column(col) == LOW) {
                raw |= (uint16_t)(1u << (row * 4u + col));
            }
        }
    }

    drive_rows_idle();
    return raw;
}

static void update_debounce(uint16_t raw)
{
    const uint16_t previous = g_debounced_state;
    uint16_t debounced = g_debounced_state;

    for (uint8_t bit = 0u; bit < MATRIX_KEY_COUNT; ++bit) {
        const uint8_t sample = (uint8_t)((raw >> bit) & 1u);
        g_debounce_history[bit] =
            (uint8_t)((g_debounce_history[bit] << 1) | sample);

        if (g_debounce_history[bit] == 0xFFu) {
            debounced |= (uint16_t)(1u << bit);
        } else if (g_debounce_history[bit] == 0x00u) {
            debounced &= (uint16_t)~(1u << bit);
        }
    }

    g_debounced_state = debounced;
    if (debounced != previous) {
        g_matrix_changed = true;
    }
}

ISR(TIMER1_COMPA_vect)
{
    const uint16_t raw = scan_raw_matrix();
    update_debounce(raw);
}

void keyboard_matrix_init(void)
{
    for (uint8_t row = 0u; row < 4u; ++row) {
        pinMode(MATRIX_ROW_PINS[row], OUTPUT);
        digitalWrite(MATRIX_ROW_PINS[row], HIGH);
    }

    for (uint8_t col = 0u; col < 4u; ++col) {
        pinMode(MATRIX_COL_PINS[col], INPUT_PULLUP);
    }

    for (uint8_t bit = 0u; bit < MATRIX_KEY_COUNT; ++bit) {
        g_debounce_history[bit] = 0u;
    }

    g_debounced_state = scan_raw_matrix();
    g_matrix_changed = false;

    /* Timer1 CTC: 16 MHz / 64 / 250 = 1 kHz (1 ms). */
    TCCR1A = 0u;
    TCCR1B = (1u << WGM12) | (1u << CS11) | (1u << CS10);
    OCR1A = 249u;
    TIMSK1 = (1u << OCIE1A);
}

void keyboard_matrix_poll(void)
{
    if (!g_matrix_changed) {
        return;
    }

    uint16_t state;
    noInterrupts();
    state = g_debounced_state;
    g_matrix_changed = false;
    interrupts();

    /* Wire format is physical row*4+col bits; Pi remaps via matrix_map.conf */
    protocol_tx_keyboard_matrix(state);
}
