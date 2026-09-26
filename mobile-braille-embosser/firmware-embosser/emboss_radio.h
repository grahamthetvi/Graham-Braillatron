/*
 * ESP32-S3 side of the embosser link. BRF bytes in, link frames out.
 * Does not step motors.
 */

#ifndef EMBOSS_RADIO_H
#define EMBOSS_RADIO_H

#include "emboss_job.h"
#include "embosser_link.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emboss_radio {
    emboss_job job;
    void *user;
    void (*tx)(void *user, const uint8_t *frame, size_t len);
} emboss_radio;

void emboss_radio_init(emboss_radio *radio,
                       void (*tx)(void *user, const uint8_t *frame, size_t len), void *user);

void emboss_radio_heartbeat(emboss_radio *radio);
void emboss_radio_feed(emboss_radio *radio, const uint8_t *data, size_t len);
void emboss_radio_finish(emboss_radio *radio);
void emboss_radio_clear_fault(emboss_radio *radio);

#ifdef __cplusplus
}
#endif

#endif /* EMBOSS_RADIO_H */
