/*
 * Spider side of the embosser link. Link frames in, motor pipeline out.
 * Motors stay disabled until a heartbeat, and a gap of EMBOSS_LINK_HEARTBEAT_MS
 * is a safety cut.
 */

#ifndef EMBOSS_SPIDER_H
#define EMBOSS_SPIDER_H

#include "emboss_pipeline.h"
#include "embosser_link.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct emboss_spider {
    emboss_pipeline *pipeline;
    embosser_link_parser parser;
    int armed;
    uint64_t last_heartbeat_ms;
    braillatron_job_state sent_state;
    braillatron_fault_reason sent_fault;
    int sent_valid;
    void *user;
    void (*tx)(void *user, const uint8_t *frame, size_t len);
} emboss_spider;

void emboss_spider_init(emboss_spider *spider, emboss_pipeline *pipeline,
                        void (*tx)(void *user, const uint8_t *frame, size_t len), void *user);

void emboss_spider_rx(emboss_spider *spider, const uint8_t *data, size_t len, uint64_t now_ms);
void emboss_spider_poll(emboss_spider *spider, uint64_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* EMBOSS_SPIDER_H */
