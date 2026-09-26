#include "fail_safes.h"

#include "mpu6050_isr.h"
#include "pins.h"
#include "telemetry_handler.h"
#include "watchdog.h"

#include <Arduino.h>

void fail_safes_init(void)
{
    pinMode(PIN_STEPPER_CUT, OUTPUT);
    digitalWrite(PIN_STEPPER_CUT, HIGH);
}

void fail_safes_cut_rail(void)
{
    digitalWrite(PIN_STEPPER_CUT, LOW);
}

void fail_safes_restore_rail(void)
{
    digitalWrite(PIN_STEPPER_CUT, HIGH);
}

void fail_safes_recover_freefall(void)
{
    mpu6050_clear_freefall();

    if (!mpu6050_isr_ready() || mpu6050_freefall_pending()) {
        return;
    }
    if (watchdog_comms_lost() || telemetry_handler_battery_critical()) {
        return;
    }

    fail_safes_restore_rail();
    if (mpu6050_freefall_pending()) {
        fail_safes_cut_rail();
    }
}
