#include "telemetry_handler.h"

#include "fail_safes.h"

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
        fail_safes_restore_rail();
    }
}
