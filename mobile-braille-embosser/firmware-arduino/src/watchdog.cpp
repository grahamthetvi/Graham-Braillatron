/*
 * Two-layer watchdog (V9 spec §4.3 / protocol.md):
 *
 *  1. AVR hardware WDT (500 ms): a hung main loop resets the MCU. The
 *     stepper rail is driven HIGH only in setup, so a reset leaves the rail
 *     down until the firmware completes a clean boot.
 *  2. Host comms watchdog: the Pi sends OP_HEARTBEAT periodically. Before
 *     the first heartbeat the check is disarmed (boot grace, the Pi may
 *     still be booting). After that, a gap longer than COMMS_TIMEOUT_MS
 *     cuts VMOT and latches a COMMS_LOSS fault until heartbeats resume.
 */

#include "watchdog.h"

#include "fail_safes.h"
#include "mpu6050_isr.h"
#include "protocol.h"
#include "protocol_tx.h"

#include <avr/wdt.h>

#define COMMS_TIMEOUT_MS 3000ul

static uint32_t g_last_heartbeat_ms = 0u;
static bool g_heartbeat_seen = false;
static bool g_comms_lost = false;

void watchdog_init(void)
{
    wdt_enable(WDTO_500MS);
}

void watchdog_kick(uint32_t now_ms)
{
    wdt_reset();

    if (!g_heartbeat_seen || g_comms_lost) {
        return;
    }

    const uint32_t elapsed = now_ms - g_last_heartbeat_ms;
    if (elapsed > COMMS_TIMEOUT_MS) {
        g_comms_lost = true;
        fail_safes_cut_rail();
        protocol_tx_safety(
            (uint8_t)BRAILLATRON_FAULT_COMMS_LOSS,
            (uint8_t)BRAILLATRON_SEVERITY_CRITICAL,
            (uint16_t)elapsed);
    }
}

void watchdog_notify_heartbeat(uint32_t now_ms)
{
    g_last_heartbeat_ms = now_ms;
    g_heartbeat_seen = true;

    if (g_comms_lost) {
        g_comms_lost = false;
        /* A latched freefall keeps the rail down regardless of comms. */
        if (!mpu6050_freefall_pending()) {
            fail_safes_restore_rail();
        }
    }
}

bool watchdog_comms_lost(void)
{
    return g_comms_lost;
}
