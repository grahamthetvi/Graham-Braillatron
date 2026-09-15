#include "telemetry_handler.h"

#include "fail_safes.h"
#include "mpu6050_isr.h"
#include "pins.h"

#include <Arduino.h>

static bool g_battery_critical_latched = false;

void telemetry_handler_init(void)
{
    g_battery_critical_latched = false;
}

void telemetry_handler_apply(const braillatron_telemetry_t *payload)
{
    if (payload == nullptr) {
        return;
    }

    if ((payload->limit_status & BRAILLATRON_LIMIT_BATTERY_CRITICAL) != 0u) {
        if (!g_battery_critical_latched) {
            g_battery_critical_latched = true;
            fail_safes_cut_rail();
        }
        return;
    }

    if (g_battery_critical_latched &&
        payload->battery_percent != BRAILLATRON_TELEMETRY_UNKNOWN &&
        payload->battery_percent > 5u) {
        g_battery_critical_latched = false;
        if (!mpu6050_freefall_pending()) {
            fail_safes_restore_rail();
        }
    }

    if ((payload->limit_status & BRAILLATRON_LIMIT_CLEAR_FREEFALL) == 0u) {
        return;
    }

    /* Active-low INT: HIGH means the latch is no longer asserted. */
    if (digitalRead(PIN_MPU6050_INT) != HIGH) {
        return;
    }

    mpu6050_clear_freefall();
    if (!g_battery_critical_latched) {
        fail_safes_restore_rail();
    }
}
