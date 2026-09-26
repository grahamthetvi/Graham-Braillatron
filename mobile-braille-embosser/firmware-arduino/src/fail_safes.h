#pragma once

/*
 * D12 / PIN_STEPPER_CUT is active-high motor-rail enable.
 * HIGH = motors powered, LOW = rail cut.
 *
 * The switch has to break Monster8 VIN+ (high side). A low-side FET on VIN-
 * is bypassed by the USB ground the Monster8 shares with the Pi. See the
 * V5.1 build guide §2.7. Setup drives the pin low; the rail stays off until
 * the host has heartbeated and no fault is holding it.
 */

void fail_safes_init(void);
void fail_safes_cut_rail(void);
void fail_safes_restore_rail(void);

/* Drive D12 from the current holds. Safe to call every main-loop pass. */
void fail_safes_apply_rail(void);

/*
 * Explicit Pi-commanded recover (BRAILLATRON_OP_CLEAR_FAULT): drop the
 * comms-loss latch, clear the MPU freefall latch, then apply the rail.
 * MPU-missing, battery-critical, and an INT that is still low keep D12 off.
 * Not called from the main loop.
 */
void fail_safes_recover_freefall(void);
