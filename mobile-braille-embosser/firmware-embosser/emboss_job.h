/*
 * Streaming BRF job parser. Emits cells, newlines, and form feeds.
 * Does not store the document and does not move motors.
 */

#ifndef EMBOSS_JOB_H
#define EMBOSS_JOB_H

#include "print_contract.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMBOSS_LINE_CAP 160
#define EMBOSS_FILENAME_CAP 128

typedef enum emboss_header_phase {
    EMBOSS_HDR_MAYBE = 0,
    EMBOSS_HDR_FILENAME,
    EMBOSS_HDR_BODY
} emboss_header_phase;

typedef struct emboss_job_cbs {
    void *user;
    void (*cell)(void *user, uint8_t dot_mask);
    void (*newline)(void *user);
    void (*form_feed)(void *user);
    void (*filename)(void *user, const char *name);
    void (*overflow)(void *user);
    int (*is_faulted)(void *user);
} emboss_job_cbs;

typedef struct emboss_job {
    emboss_job_cbs cbs;
    emboss_header_phase hdr;
    int swallow_lf;
    char line[EMBOSS_LINE_CAP];
    size_t line_len;
} emboss_job;

void emboss_job_init(emboss_job *job, const emboss_job_cbs *cbs);
void emboss_job_reset(emboss_job *job);
void emboss_job_feed(emboss_job *job, const uint8_t *data, size_t len);

/* Flush a partial header as cells. Does not mean the motion job is done. */
void emboss_job_finish(emboss_job *job);

#ifdef __cplusplus
}
#endif

#endif /* EMBOSS_JOB_H */
