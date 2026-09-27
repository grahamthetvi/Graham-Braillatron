/*
 * Host-testable step list for the Spider's eight driver sockets.
 * Socket index is not a pin number. No ESP-IDF and no STM32 HAL.
 *
 *   0     X carriage
 *   1     Y tractor
 *   2..7  emboss_1 .. emboss_6, dot bits 0..5
 *
 * The named drive constants are the host-test defaults (200 full steps,
 * 16 microsteps, 40 mm X, 20 mm Y, 40 mm punch, 2 mm stroke). A different
 * pulley is a different emboss_axis_drive, not a code change.
 */

#ifndef EMBOSS_STEPS_H
#define EMBOSS_STEPS_H

#include "emboss_pipeline.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMBOSS_STEPS_SOCKET_X 0u
#define EMBOSS_STEPS_SOCKET_Y 1u
#define EMBOSS_STEPS_SOCKET_EMBOSS_BASE 2u

#define EMBOSS_STEPS_FULL_STEPS_PER_REV 200u
#define EMBOSS_STEPS_MICROSTEPS 16u
#define EMBOSS_STEPS_X_ROTATION_UM 40000u
#define EMBOSS_STEPS_Y_ROTATION_UM 20000u
#define EMBOSS_STEPS_PUNCH_ROTATION_UM 40000u
#define EMBOSS_STEPS_PUNCH_STROKE_UM 2000u

typedef struct emboss_axis_drive {
    uint32_t rotation_distance_um;
    uint32_t full_steps_per_rev;
    uint32_t microsteps;
} emboss_axis_drive;

typedef struct emboss_steps {
    emboss_axis_drive x_drive;
    emboss_axis_drive y_drive;
    emboss_axis_drive punch_drive;
    uint32_t punch_stroke_um;
    void *user;
    void (*emit)(void *user, uint8_t socket, int32_t steps);
} emboss_steps;

/* Truncating division toward zero. 64-bit intermediate. Zero if the drive cannot step. */
int32_t emboss_steps_from_um(int32_t delta_um, const emboss_axis_drive *drive);

void emboss_steps_init(emboss_steps *steps, const emboss_axis_drive *x_drive,
                       const emboss_axis_drive *y_drive, const emboss_axis_drive *punch_drive,
                       uint32_t punch_stroke_um,
                       void (*emit)(void *user, uint8_t socket, int32_t steps), void *user);

/* Fills travel_x, feed_y, and strike. set_enable stays NULL. */
void emboss_steps_as_motor(emboss_steps *steps, emboss_motor *out);

#ifdef __cplusplus
}
#endif

#endif /* EMBOSS_STEPS_H */
