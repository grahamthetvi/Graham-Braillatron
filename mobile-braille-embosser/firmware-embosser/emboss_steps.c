#include "emboss_steps.h"

#include <string.h>

int32_t emboss_steps_from_um(int32_t delta_um, const emboss_axis_drive *drive)
{
    int64_t steps;

    if (drive == NULL || drive->rotation_distance_um == 0) {
        return 0;
    }
    steps = (int64_t)delta_um * (int64_t)drive->full_steps_per_rev * (int64_t)drive->microsteps /
            (int64_t)drive->rotation_distance_um;
    return (int32_t)steps;
}

static void emit_axis(emboss_steps *steps, uint8_t socket, int32_t delta_um,
                      const emboss_axis_drive *drive)
{
    int32_t count;

    if (steps->emit == NULL || delta_um == 0) {
        return;
    }
    count = emboss_steps_from_um(delta_um, drive);
    if (count == 0) {
        return;
    }
    steps->emit(steps->user, socket, count);
}

static void steps_travel_x(void *user, int32_t delta_um)
{
    emboss_steps *steps = user;

    emit_axis(steps, EMBOSS_STEPS_SOCKET_X, delta_um, &steps->x_drive);
}

static void steps_feed_y(void *user, int32_t delta_um)
{
    emboss_steps *steps = user;

    emit_axis(steps, EMBOSS_STEPS_SOCKET_Y, delta_um, &steps->y_drive);
}

static void steps_strike(void *user, uint8_t dot_mask, int32_t x_um)
{
    emboss_steps *steps = user;
    int32_t punch;
    unsigned bit;

    (void)x_um;
    if (steps->emit == NULL || dot_mask == 0) {
        return;
    }
    punch = emboss_steps_from_um((int32_t)steps->punch_stroke_um, &steps->punch_drive);
    if (punch == 0) {
        return;
    }
    for (bit = 0; bit < 6; bit++) {
        if ((dot_mask & (uint8_t)(1u << bit)) == 0) {
            continue;
        }
        steps->emit(steps->user, (uint8_t)(EMBOSS_STEPS_SOCKET_EMBOSS_BASE + bit), punch);
        steps->emit(steps->user, (uint8_t)(EMBOSS_STEPS_SOCKET_EMBOSS_BASE + bit), -punch);
    }
}

void emboss_steps_init(emboss_steps *steps, const emboss_axis_drive *x_drive,
                       const emboss_axis_drive *y_drive, const emboss_axis_drive *punch_drive,
                       uint32_t punch_stroke_um,
                       void (*emit)(void *user, uint8_t socket, int32_t steps), void *user)
{
    if (steps == NULL) {
        return;
    }
    memset(steps, 0, sizeof *steps);
    if (x_drive != NULL) {
        steps->x_drive = *x_drive;
    }
    if (y_drive != NULL) {
        steps->y_drive = *y_drive;
    }
    if (punch_drive != NULL) {
        steps->punch_drive = *punch_drive;
    }
    steps->punch_stroke_um = punch_stroke_um;
    steps->emit = emit;
    steps->user = user;
}

void emboss_steps_as_motor(emboss_steps *steps, emboss_motor *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    if (steps == NULL) {
        return;
    }
    out->user = steps;
    out->strike = steps_strike;
    out->travel_x = steps_travel_x;
    out->feed_y = steps_feed_y;
}
