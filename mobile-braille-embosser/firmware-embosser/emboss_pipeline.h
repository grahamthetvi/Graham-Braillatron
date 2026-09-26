/*
 * Host-testable embosser pipeline. No stepper board, no Arduino, no ESP-IDF.
 * Motor pulses are a struct of function pointers until a driver board exists.
 */

#ifndef EMBOSS_PIPELINE_H
#define EMBOSS_PIPELINE_H

#include "print_contract.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMBOSS_LINE_CAP 160
#define EMBOSS_FILENAME_CAP 128
#define EMBOSS_ROW_B_CAP 8

typedef enum emboss_header_phase {
    EMBOSS_HDR_MAYBE = 0,
    EMBOSS_HDR_FILENAME,
    EMBOSS_HDR_BODY
} emboss_header_phase;

typedef struct emboss_pending_row_b {
    int32_t due_x_um;
    uint8_t mask;
} emboss_pending_row_b;

/*
 * strike is the row mask (Row A or Row B bits only) at absolute carriage X.
 * feed_y delta is micrometres; positive feeds paper forward.
 * set_enable: nonzero allows the motors to run. A NULL strike is allowed;
 * position is still tracked. Tests should pass a recorder.
 */
typedef struct emboss_motor {
    void *user;
    void (*strike)(void *user, uint8_t dot_mask, int32_t x_um);
    void (*feed_y)(void *user, int32_t delta_um);
    void (*set_enable)(void *user, int enabled);
} emboss_motor;

typedef struct emboss_pipeline {
    emboss_motor motor;
    char *status;
    size_t status_len;
    braillatron_job_state state;
    braillatron_fault_reason fault;
    int motors_enabled;
    int32_t x_um;
    int32_t y_um;
    int32_t y_line;
    int line_open;
    int swallow_lf;
    emboss_header_phase hdr;
    char line[EMBOSS_LINE_CAP];
    size_t line_len;
    char filename[EMBOSS_FILENAME_CAP];
    emboss_pending_row_b pending[EMBOSS_ROW_B_CAP];
    size_t pending_head;
    size_t pending_count;
} emboss_pipeline;

void emboss_pipeline_init(emboss_pipeline *pipeline, const emboss_motor *motor,
                          char *status, size_t status_len);

/* Stream BRF bytes. Does not store the document. */
void emboss_pipeline_feed(emboss_pipeline *pipeline, const uint8_t *data, size_t len);

/* Close the open job. The caller applies the 1500 ms idle gap. */
void emboss_pipeline_finish(emboss_pipeline *pipeline);

/* Drop cuts motor power. No further strikes until clear_fault. */
void emboss_pipeline_safety_cut(emboss_pipeline *pipeline);

/* Paper, motion, or overflow. Disables motors and publishes the reason. */
void emboss_pipeline_fault(emboss_pipeline *pipeline, braillatron_fault_reason reason);

/* Leave fault, re-enable motors. Does not home the carriage. */
void emboss_pipeline_clear_fault(emboss_pipeline *pipeline);

int32_t emboss_pipeline_x_um(const emboss_pipeline *pipeline);
int32_t emboss_pipeline_y_um(const emboss_pipeline *pipeline);
int32_t emboss_pipeline_y_line(const emboss_pipeline *pipeline);
braillatron_job_state emboss_pipeline_state(const emboss_pipeline *pipeline);
int emboss_pipeline_motors_enabled(const emboss_pipeline *pipeline);
const char *emboss_pipeline_filename(const emboss_pipeline *pipeline);

#ifdef __cplusplus
}
#endif

#endif /* EMBOSS_PIPELINE_H */
