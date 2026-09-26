#pragma once

/* Drives the TC4420 gate (PIN_STEPPER_CUT / D12): HIGH = VMOT on, LOW = rail cut. */

void fail_safes_init(void);
void fail_safes_cut_rail(void);
void fail_safes_restore_rail(void);

/*
 * Explicit Pi-commanded recover (BRAILLATRON_OP_CLEAR_FAULT): clear the MPU
 * freefall latch and restore D12 if no other hold is active. Not called from
 * the main loop.
 */
void fail_safes_recover_freefall(void);
