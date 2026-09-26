#include "fail_safes.h"

#include "mpu6050_isr.h"
#include "pins.h"
#include "telemetry_handler.h"
#include "watchdog.h"

#include <Arduino.h>

void fail_safes_init(void)
{
    /* Write low before switching to output so the pin cannot glitch high. */
    digitalWrite(PIN_STEPPER_CUT, LOW);
    pinMode(PIN_STEPPER_CUT, OUTPUT);
    digitalWrite(PIN_STEPPER_CUT, LOW);
}

void fail_safes_cut_rail(void)
{
    digitalWrite(PIN_STEPPER_CUT, LOW);
}

void fail_safes_restore_rail(void)
{
    digitalWrite(PIN_STEPPER_CUT, HIGH);
}

void fail_safes_apply_rail(void)
{
    const bool hold = !watchdog_host_seen() || watchdog_comms_lost() ||
                      !mpu6050_isr_ready() || mpu6050_freefall_pending() ||
                      telemetry_handler_battery_critical();
    if (hold) {
        fail_safes_cut_rail();
        return;
    }

    fail_safes_restore_rail();
    /* The freefall ISR can land between the check above and this write. */
    if (mpu6050_freefall_pending()) {
        fail_safes_cut_rail();
    }
}

void fail_safes_recover_freefall(void)
{
    watchdog_clear_comms_lost(millis());
    mpu6050_clear_freefall();
    fail_safes_apply_rail();
}
