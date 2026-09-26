#pragma once

#include <stdbool.h>

/* Returns false when the MPU6050 is missing or any config write fails. */
bool mpu6050_isr_init(void);

/* True after a successful mpu6050_isr_init(); VMOT must stay cut otherwise. */
bool mpu6050_isr_ready(void);

bool mpu6050_freefall_pending(void);

/*
 * Clears the MPU interrupt latch (INT_STATUS read) and the pending flag.
 * Host-command recovery only — never call from the main loop poll.
 */
void mpu6050_clear_freefall(void);
