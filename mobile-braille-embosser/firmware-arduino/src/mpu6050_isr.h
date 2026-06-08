#pragma once

#include <stdbool.h>

void mpu6050_isr_init(void);
bool mpu6050_freefall_pending(void);
void mpu6050_clear_freefall(void);
