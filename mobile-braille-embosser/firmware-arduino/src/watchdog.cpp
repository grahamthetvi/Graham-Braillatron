/*
 * Two-layer watchdog (V9 spec §4.3 / protocol.md):
 *
 *  1. AVR hardware WDT (500 ms): a hung main loop resets the MCU. Reset
 *     floats D12 until setup drives it low. VMOT stays off until the first
 *     heartbeat and a good MPU init (fail_safes_apply_rail). A hardware
 *     pulldown on the switch enable is still required so the pin cannot
 *     float the rail on during the bootloader.
 *  2. Host comms watchdog: before the first heartbeat the rail stays off
 *     (the Pi may still be booting). After that, a gap longer than
 *     COMMS_TIMEOUT_MS cuts VMOT and latches COMMS_LOSS until the Pi sends
 *     CLEAR_FAULT. The next heartbeat does not turn the rail back on.
 */

#include "watchdog.h"

#include "fail_safes.h"

#include <avr/wdt.h>

#define COMMS_TIMEOUT_MS 3000ul

static uint32_t g_last_heartbeat_ms = 0u;
static bool g_heartbeat_seen = false;
static bool g_comms_lost = false;
static uint16_t g_comms_elapsed_ms = 0u;

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
        g_comms_elapsed_ms = elapsed > 0xFFFFul ? 0xFFFFu : (uint16_t)elapsed;
        fail_safes_cut_rail();
        /* The main loop rebroadcasts COMMS_LOSS. Not FAULT_WATCHDOG_TIMEOUT. */
    }
}

void watchdog_notify_heartbeat(uint32_t now_ms)
{
    g_last_heartbeat_ms = now_ms;
    g_heartbeat_seen = true;
}

void watchdog_clear_comms_lost(uint32_t now_ms)
{
    g_comms_lost = false;
    g_comms_elapsed_ms = 0u;
    g_heartbeat_seen = true;
    g_last_heartbeat_ms = now_ms;
}

bool watchdog_host_seen(void)
{
    return g_heartbeat_seen;
}

bool watchdog_comms_lost(void)
{
    return g_comms_lost;
}

uint16_t watchdog_comms_elapsed_ms(void)
{
    return g_comms_elapsed_ms;
}
