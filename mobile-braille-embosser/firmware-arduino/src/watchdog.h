#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Arms the AVR hardware watchdog (500 ms). Call last in setup. */
void watchdog_init(void);

/*
 * Resets the hardware watchdog and checks the host heartbeat. Once a first
 * heartbeat has been seen, a gap longer than the comms timeout cuts the
 * stepper rail and latches BRAILLATRON_FAULT_COMMS_LOSS until CLEAR_FAULT.
 * A later heartbeat does not clear that latch.
 */
void watchdog_kick(uint32_t now_ms);

/* Called by the serial RX parser on every valid heartbeat frame. */
void watchdog_notify_heartbeat(uint32_t now_ms);

/* CLEAR_FAULT: the host is talking again. Also refreshes the heartbeat clock. */
void watchdog_clear_comms_lost(uint32_t now_ms);

bool watchdog_host_seen(void);
bool watchdog_comms_lost(void);
uint16_t watchdog_comms_elapsed_ms(void);
