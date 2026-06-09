#pragma once

/*
 * PROFILE: skeleton_v5
 * Arduino Micro (ATmega32U4) pin map — V5.1 direct-pin topology.
 *
 * The 4x4 matrix, steering diodes, and external pull-up resistors are
 * abandoned (V5.1 Build Guide, Part 3.1). One side of every switch ties to a
 * common ground bus; the other side routes to its own dedicated pin and uses
 * the internal pull-up (active LOW).
 */

#include <Arduino.h>
#include <stdint.h>

#include "protocol.h"

#define PIN_UART_TX             1u
#define PIN_UART_RX             0u

/* Hardware I2C (Wire library): SDA = D2, SCL = D3. */
#define PIN_I2C_SDA             2u
#define PIN_I2C_SCL             3u

#define PIN_MPU6050_INT         7u  /* INT6 (PE6); active-low, latched */

/* TC4420 gate driver input; HIGH = stepper rail enabled, LOW = rail cut. */
#define PIN_STEPPER_CUT        12u  /* PD6 */
#define STEPPER_CUT_PORT        PORTD
#define STEPPER_CUT_BIT         6u

#define BUTTON_COUNT           13u

/*
 * V5.1 Build Guide Part 3.1 wiring. Button 8 lives on A4 (moved off pin 13
 * to avoid the onboard LED circuitry). Button 13 (Menu) is an addition on A5
 * to cover all 13 logical keys defined in shared/protocol.h.
 */
static const uint8_t BUTTON_PINS[BUTTON_COUNT] = {
    4u,        /* Button 1  — dot 1 */
    5u,        /* Button 2  — dot 2 */
    6u,        /* Button 3  — dot 3 */
    8u,        /* Button 4  — dot 4 */
    9u,        /* Button 5  — dot 5 */
    10u,       /* Button 6  — dot 6 */
    11u,       /* Button 7  — D-pad up */
    (uint8_t)A4, /* Button 8  — D-pad down */
    (uint8_t)A0, /* Button 9  — backspace */
    (uint8_t)A1, /* Button 10 — enter */
    (uint8_t)A2, /* Button 11 — shift / TTS */
    (uint8_t)A3, /* Button 12 — speech (push-to-talk) */
    (uint8_t)A5, /* Button 13 — menu */
};

/* Logical protocol bit transmitted for each button (shared/protocol.h). */
static const uint16_t BUTTON_KEY_BITS[BUTTON_COUNT] = {
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
};

#define UART_BAUD_RATE      115200u
