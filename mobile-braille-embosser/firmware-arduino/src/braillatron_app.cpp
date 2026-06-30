#include "braillatron_app.h"

#include "chord_engine.h"
#include "fail_safes.h"
#include "keyboard_buttons.h"
#include "mpu6050_isr.h"
#include "protocol_rx.h"
#include "protocol_tx.h"
#include "telemetry_handler.h"
#include "watchdog.h"

#include "protocol.h"

#include <Arduino.h>

/*
 * Active latched faults are rebroadcast on this interval so a host that
 * boots (or reconnects) after the fault still learns about it.
 */
#define FAULT_REBROADCAST_MS 1000ul

static bool g_sensor_fault = false;
static uint32_t g_last_fault_tx_ms = 0u;
static bool g_fault_tx_pending = false;

static void broadcast_active_fault(uint32_t now_ms)
{
    uint8_t fault_code = BRAILLATRON_FAULT_NONE;
    uint8_t severity = BRAILLATRON_SEVERITY_INFO;

    if (mpu6050_freefall_pending()) {
        fault_code = BRAILLATRON_FAULT_FREEFALL;
        severity = BRAILLATRON_SEVERITY_LATCHED;
    } else if (g_sensor_fault) {
        fault_code = BRAILLATRON_FAULT_SENSOR_FAILURE;
        severity = BRAILLATRON_SEVERITY_CRITICAL;
    }

    if (fault_code == BRAILLATRON_FAULT_NONE) {
        return;
    }

    if (g_fault_tx_pending && (uint32_t)(now_ms - g_last_fault_tx_ms) < FAULT_REBROADCAST_MS) {
        return;
    }

    protocol_tx_safety(fault_code, severity, 0u);
    g_last_fault_tx_ms = now_ms;
    g_fault_tx_pending = true;
}

void braillatron_setup(void)
{
    fail_safes_init();
    telemetry_handler_init();
    protocol_tx_init();
    protocol_rx_init();
    keyboard_buttons_init();
    chord_engine_init();

    /* Freefall protection is mandatory: without the sensor, keep VMOT down
     * and report SENSOR_FAILURE so the Pi blocks motion and warns the user. */
    if (!mpu6050_isr_init()) {
        g_sensor_fault = true;
        fail_safes_cut_rail();
    }

    /* Armed last so slow init paths cannot trip the hardware WDT. */
    watchdog_init();
}

void braillatron_loop(void)
{
    const uint32_t now_ms = millis();

    protocol_rx_poll(now_ms);
    watchdog_kick(now_ms);
    broadcast_active_fault(now_ms);

    if (mpu6050_freefall_pending()) {
        /* Latched: input is disabled, but serial and watchdog stay alive. */
        return;
    }

    const bool changed = keyboard_buttons_poll(now_ms);
    chord_engine_update(keyboard_buttons_state(), changed, now_ms);
}
