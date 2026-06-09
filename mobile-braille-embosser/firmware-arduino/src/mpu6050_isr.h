#pragma once

#include <stdbool.h>

/* Returns false when the MPU6050 is missing or any config write fails. */
bool mpu6050_isr_init(void);

bool mpu6050_freefall_pending(void);

/* Clears the MPU interrupt latch and the pending flag (explicit recovery). */
void mpu6050_clear_freefall(void);
