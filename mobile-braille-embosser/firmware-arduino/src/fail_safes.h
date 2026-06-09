#pragma once

/* Drives the TC4420 gate (PIN_STEPPER_CUT): HIGH = VMOT on, LOW = rail cut. */

void fail_safes_init(void);
void fail_safes_cut_rail(void);
void fail_safes_restore_rail(void);
