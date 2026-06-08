#pragma once

/*
 * PROFILE: skeleton_v4
 * Arduino Micro (ATmega32U4) pin map — prototype wiring.
 * Matrix rows 5,6,8,9; cols 10-13; stepper cut pin 4; MPU INT0 pin 3.
 * Bit-banged I2C: SDA=2, SCL=7.
 */

#include <stdint.h>

#define PIN_UART_TX             1u
#define PIN_UART_RX             0u

#define PIN_I2C_SDA             2u
#define PIN_I2C_SCL             7u

#define PIN_MPU6050_INT         3u  /* INT0 (PD0) */

#define PIN_STEPPER_CUT         4u  /* TC4420 gate driver; LOW = rail off */

#define PIN_MATRIX_ROW_0        5u
#define PIN_MATRIX_ROW_1        6u
#define PIN_MATRIX_ROW_2        8u
#define PIN_MATRIX_ROW_3        9u

#define PIN_MATRIX_COL_0       10u
#define PIN_MATRIX_COL_1       11u
#define PIN_MATRIX_COL_2       12u
#define PIN_MATRIX_COL_3       13u

static const uint8_t MATRIX_ROW_PINS[4] = {
    PIN_MATRIX_ROW_0,
    PIN_MATRIX_ROW_1,
    PIN_MATRIX_ROW_2,
    PIN_MATRIX_ROW_3,
};

static const uint8_t MATRIX_COL_PINS[4] = {
    PIN_MATRIX_COL_0,
    PIN_MATRIX_COL_1,
    PIN_MATRIX_COL_2,
    PIN_MATRIX_COL_3,
};

#define UART_BAUD_RATE      115200u
