/*
 * MPU6050 freefall interlock (V5.1 Part 3.3 / 4.2).
 *
 * Uses hardware I2C (Wire, SDA = D2, SCL = D3) and the MPU6050's native
 * free-fall detection engine. INT is Arduino D7 / PE6 / INT6 — never D3 /
 * INT0 (that pin is SCL). The interrupt line is configured active-low
 * (INT_PIN_CFG=0xA0) and latched, sampled FALLING; with INPUT_PULLUP a
 * disconnected wire idles HIGH and cannot false-trigger.
 *
 * The ISR cuts D12/VMOT with a direct port write so the <10 ms interlock
 * budget holds regardless of main-loop activity. The latch is cleared only
 * by an explicit BRAILLATRON_OP_CLEAR_FAULT from the Pi — never on loop.
 */

#include "mpu6050_isr.h"

#include "pins.h"

#include <Arduino.h>
#include <Wire.h>

#define MPU6050_ADDR                0x68u

#define MPU6050_REG_FF_THR          0x1Du
#define MPU6050_REG_FF_DUR          0x1Eu
#define MPU6050_REG_ACCEL_CONFIG    0x1Cu
#define MPU6050_REG_INT_PIN_CFG     0x37u
#define MPU6050_REG_INT_ENABLE      0x38u
#define MPU6050_REG_INT_STATUS      0x3Au
#define MPU6050_REG_PWR_MGMT_1      0x6Bu
#define MPU6050_REG_WHO_AM_I        0x75u

/* Free-fall threshold/duration per V5.1 Part 4.2 (1 LSB = 2 mg / 1 ms). */
#define MPU6050_FF_THR_VALUE        0x30u
#define MPU6050_FF_DUR_VALUE        0x14u

#define MPU6050_INT_PIN_CFG_VALUE   0xA0u /* active-low, latched until read */
#define MPU6050_INT_ENABLE_FF       0x80u /* FF_EN */

static volatile bool g_freefall_pending = false;
static bool g_mpu_ready = false;

static bool mpu6050_write_reg(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission((uint8_t)MPU6050_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0u;
}

static bool mpu6050_read_reg(uint8_t reg, uint8_t *value)
{
    Wire.beginTransmission((uint8_t)MPU6050_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0u) {
        return false;
    }
    if (Wire.requestFrom((uint8_t)MPU6050_ADDR, (uint8_t)1u) != 1u) {
        return false;
    }
    *value = (uint8_t)Wire.read();
    return true;
}

static void freefall_isr(void)
{
    /* Direct port write: sub-10 ms VMOT isolation must not wait on the loop. */
    STEPPER_CUT_PORT &= (uint8_t)~(1u << STEPPER_CUT_BIT);
    g_freefall_pending = true;
}

bool mpu6050_isr_init(void)
{
    Wire.begin();
    Wire.setClock(400000ul);

    uint8_t who_am_i = 0u;
    if (!mpu6050_read_reg(MPU6050_REG_WHO_AM_I, &who_am_i) ||
        who_am_i != MPU6050_ADDR) {
        return false;
    }

    bool ok = true;
    ok = mpu6050_write_reg(MPU6050_REG_PWR_MGMT_1, 0x00u) && ok; /* wake; all axes on */
    delay(10);
    ok = mpu6050_write_reg(MPU6050_REG_ACCEL_CONFIG, 0x00u) && ok; /* +/-2 g */
    ok = mpu6050_write_reg(MPU6050_REG_FF_THR, MPU6050_FF_THR_VALUE) && ok;
    ok = mpu6050_write_reg(MPU6050_REG_FF_DUR, MPU6050_FF_DUR_VALUE) && ok;
    ok = mpu6050_write_reg(MPU6050_REG_INT_PIN_CFG, MPU6050_INT_PIN_CFG_VALUE) && ok;
    ok = mpu6050_write_reg(MPU6050_REG_INT_ENABLE, MPU6050_INT_ENABLE_FF) && ok;

    /* Clear any stale interrupt latch before arming the pin interrupt. */
    uint8_t status = 0u;
    ok = mpu6050_read_reg(MPU6050_REG_INT_STATUS, &status) && ok;

    pinMode(PIN_MPU6050_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_MPU6050_INT), freefall_isr, FALLING);

    g_mpu_ready = ok;
    return ok;
}

bool mpu6050_isr_ready(void)
{
    return g_mpu_ready;
}

bool mpu6050_freefall_pending(void)
{
    return g_freefall_pending;
}

void mpu6050_clear_freefall(void)
{
    uint8_t status = 0u;
    (void)mpu6050_read_reg(MPU6050_REG_INT_STATUS, &status);
    /* Active-low latched INT: if still LOW after the status read, the event
     * is ongoing and there may be no new FALLING edge — keep the latch. */
    if (digitalRead(PIN_MPU6050_INT) == LOW) {
        g_freefall_pending = true;
        return;
    }
    g_freefall_pending = false;
}
