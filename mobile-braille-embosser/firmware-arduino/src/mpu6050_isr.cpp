#include "mpu6050_isr.h"

#include "pins.h"

#include <Arduino.h>
#include <avr/interrupt.h>
#include <avr/io.h>

#define MPU6050_ADDR            0x68u

#define MPU6050_REG_PWR_MGMT_1  0x6Bu
#define MPU6050_REG_ACCEL_CONFIG 0x1Cu
#define MPU6050_REG_FF_THR      0x1Du
#define MPU6050_REG_FF_DUR      0x1Eu
#define MPU6050_REG_INT_PIN_CFG 0x37u
#define MPU6050_REG_INT_ENABLE  0x38u
#define MPU6050_REG_INT_STATUS  0x3Au
#define MPU6050_REG_MOT_DETECT_CTRL 0x69u
#define MPU6050_REG_PWR_MGMT_2  0x6Cu

/*
 * Freefall threshold ~500 mg (1 LSB = 2 mg). Duration 1 ms minimum.
 * Triggers when total acceleration stays below threshold continuously.
 */
#define MPU6050_FF_THR_VALUE    0xFAu
#define MPU6050_FF_DUR_VALUE    0x01u

static volatile bool g_freefall_pending = false;

static void i2c_sda_high(void)
{
    pinMode(PIN_I2C_SDA, INPUT_PULLUP);
}

static void i2c_sda_low(void)
{
    pinMode(PIN_I2C_SDA, OUTPUT);
    digitalWrite(PIN_I2C_SDA, LOW);
}

static void i2c_scl_high(void)
{
    pinMode(PIN_I2C_SCL, INPUT_PULLUP);
}

static void i2c_scl_low(void)
{
    pinMode(PIN_I2C_SCL, OUTPUT);
    digitalWrite(PIN_I2C_SCL, LOW);
}

static bool i2c_sda_read(void)
{
    return digitalRead(PIN_I2C_SDA) == HIGH;
}

static void i2c_delay(void)
{
    delayMicroseconds(5);
}

static bool i2c_start(void)
{
    i2c_sda_high();
    i2c_scl_high();
    i2c_delay();
    if (!i2c_sda_read()) {
        return false;
    }
    i2c_sda_low();
    i2c_delay();
    i2c_scl_low();
    i2c_delay();
    return true;
}

static void i2c_stop(void)
{
    i2c_sda_low();
    i2c_delay();
    i2c_scl_high();
    i2c_delay();
    i2c_sda_high();
    i2c_delay();
}

static bool i2c_write_byte(uint8_t value)
{
    for (int8_t bit = 7; bit >= 0; --bit) {
        if ((value >> bit) & 1u) {
            i2c_sda_high();
        } else {
            i2c_sda_low();
        }
        i2c_delay();
        i2c_scl_high();
        i2c_delay();
        i2c_scl_low();
        i2c_delay();
    }

    i2c_sda_high();
    i2c_delay();
    i2c_scl_high();
    i2c_delay();
    const bool ack = !i2c_sda_read();
    i2c_scl_low();
    i2c_delay();
    return ack;
}

static uint8_t i2c_read_byte(bool ack)
{
    uint8_t value = 0u;
    i2c_sda_high();

    for (int8_t bit = 7; bit >= 0; --bit) {
        i2c_delay();
        i2c_scl_high();
        i2c_delay();
        value = (uint8_t)((value << 1) | (i2c_sda_read() ? 1u : 0u));
        i2c_scl_low();
        i2c_delay();
    }

    if (ack) {
        i2c_sda_low();
    } else {
        i2c_sda_high();
    }
    i2c_delay();
    i2c_scl_high();
    i2c_delay();
    i2c_scl_low();
    i2c_delay();
    i2c_sda_high();

    return value;
}

static bool mpu6050_write_reg(uint8_t reg, uint8_t value)
{
    if (!i2c_start()) {
        return false;
    }
    if (!i2c_write_byte((uint8_t)(MPU6050_ADDR << 1))) {
        i2c_stop();
        return false;
    }
    if (!i2c_write_byte(reg)) {
        i2c_stop();
        return false;
    }
    if (!i2c_write_byte(value)) {
        i2c_stop();
        return false;
    }
    i2c_stop();
    return true;
}

static bool mpu6050_read_reg(uint8_t reg, uint8_t *value)
{
    if (!i2c_start()) {
        return false;
    }
    if (!i2c_write_byte((uint8_t)(MPU6050_ADDR << 1))) {
        i2c_stop();
        return false;
    }
    if (!i2c_write_byte(reg)) {
        i2c_stop();
        return false;
    }
    if (!i2c_start()) {
        return false;
    }
    if (!i2c_write_byte((uint8_t)((MPU6050_ADDR << 1) | 1u))) {
        i2c_stop();
        return false;
    }
    *value = i2c_read_byte(false);
    i2c_stop();
    return true;
}

static void stepper_cut_assert_off(void)
{
    PORTD &= ~(1u << PORTD4);
}

ISR(INT0_vect)
{
    stepper_cut_assert_off();
    g_freefall_pending = true;
}

static void int0_attach(void)
{
    pinMode(PIN_MPU6050_INT, INPUT_PULLUP);
    EICRA = (1u << ISC01);
    EIMSK |= (1u << INT0);
}

void mpu6050_isr_init(void)
{
    pinMode(PIN_I2C_SDA, INPUT_PULLUP);
    pinMode(PIN_I2C_SCL, INPUT_PULLUP);

    mpu6050_write_reg(MPU6050_REG_PWR_MGMT_1, 0x00u);
    delay(10);
    mpu6050_write_reg(MPU6050_REG_PWR_MGMT_2, 0x07u);
    mpu6050_write_reg(MPU6050_REG_ACCEL_CONFIG, 0x00u);
    mpu6050_write_reg(MPU6050_REG_FF_THR, MPU6050_FF_THR_VALUE);
    mpu6050_write_reg(MPU6050_REG_FF_DUR, MPU6050_FF_DUR_VALUE);
    mpu6050_write_reg(MPU6050_REG_MOT_DETECT_CTRL, 0x00u);
    mpu6050_write_reg(MPU6050_REG_INT_PIN_CFG, 0xA0u);
    mpu6050_write_reg(MPU6050_REG_INT_ENABLE, 0x40u);

    uint8_t status = 0u;
    mpu6050_read_reg(MPU6050_REG_INT_STATUS, &status);
    (void)status;

    int0_attach();
}

bool mpu6050_freefall_pending(void)
{
    return g_freefall_pending;
}

void mpu6050_clear_freefall(void)
{
    uint8_t status = 0u;
    mpu6050_read_reg(MPU6050_REG_INT_STATUS, &status);
    (void)status;
    g_freefall_pending = false;
}
