#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Arms the AVR hardware watchdog (500 ms). Call last in setup. */
void watchdog_init(void);

/*
 * Resets the hardware watchdog and checks the host heartbeat. Once a first
 * heartbeat has been seen, a gap longer than the comms timeout cuts the
 * stepper rail and broadcasts BRAILLATRON_FAULT_COMMS_LOSS.
 */
void watchdog_kick(uint32_t now_ms);

/* Called by the serial RX parser on every valid heartbeat frame. */
void watchdog_notify_heartbeat(uint32_t now_ms);

bool watchdog_comms_lost(void);
