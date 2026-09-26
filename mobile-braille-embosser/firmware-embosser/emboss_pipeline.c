#include "emboss_pipeline.h"

#include <string.h>

/*
 * Firing moment, matching EmbossScheduler / MotionService::emboss_dot_mask:
 *
 * Row A strikes when the cell is submitted, at that carriage X. The carriage
 * then advances BRAILLATRON_CELL_PITCH_UM (6000), including a blank cell.
 * Row B is due once travel since the Row A strike reaches
 * BRAILLATRON_ROW_B_OFFSET_UM, so it fires partway through that advance at
 * absolute X = row_a_x + 2500. The Pi records the same fire_at (row A
 * position plus the offset) even though the travel log jumps a full cell in
 * one step rather than sampling the point in between.
 *
 * A newline then travels one more offset, the same extra log as
 * MotionService::advance_line, so a Row B still inside that window fires
 * before the carriage returns to X = 0 and Y advances one line. With a
 * 6.0 mm cell and a 2.5 mm offset the queue is already empty here. Any
 * strike still pending past that window is dropped on the return. The Pi
 * leaves those entries in the delay line across reset_position(0); that
 * only matters when a fire position sits beyond the flush, which this
 * geometry does not produce.
 *
 * The first line is held only while it still matches the "BRF1 " prefix
 * (a tab after "BRF1" is accepted, as in BrfCableParser). A byte that
 * breaks the prefix is punched, including a leading "B" once the next byte
 * or finish() shows it is not the header.
 */

static void publish_status(emboss_pipeline *pipeline)
{
    if (pipeline->status == NULL || pipeline->status_len == 0) {
        return;
    }
    (void)braillatron_print_status_line(pipeline->status, pipeline->status_len,
                                        pipeline->state, pipeline->fault);
}

static void set_state(emboss_pipeline *pipeline, braillatron_job_state state)
{
    braillatron_fault_reason fault = BRAILLATRON_PRINT_FAULT_NONE;

    if (state == BRAILLATRON_JOB_FAULT) {
        fault = pipeline->fault;
    }
    if (pipeline->state == state && pipeline->fault == fault) {
        return;
    }
    pipeline->state = state;
    pipeline->fault = fault;
    publish_status(pipeline);
}

static void enter_fault(emboss_pipeline *pipeline, braillatron_fault_reason reason)
{
    pipeline->motors_enabled = 0;
    pipeline->pending_count = 0;
    if (pipeline->motor.set_enable != NULL) {
        pipeline->motor.set_enable(pipeline->motor.user, 0);
    }
    pipeline->fault = reason;
    pipeline->state = BRAILLATRON_JOB_IDLE;
    set_state(pipeline, BRAILLATRON_JOB_FAULT);
}

static void do_strike(emboss_pipeline *pipeline, uint8_t mask, int32_t x_um)
{
    if (!pipeline->motors_enabled || pipeline->state == BRAILLATRON_JOB_FAULT) {
        return;
    }
    if (mask == 0 || pipeline->motor.strike == NULL) {
        return;
    }
    pipeline->motor.strike(pipeline->motor.user, mask, x_um);
}

static void pop_pending(emboss_pipeline *pipeline)
{
    if (pipeline->pending_count == 0) {
        return;
    }
    pipeline->pending_head = (pipeline->pending_head + 1) % EMBOSS_ROW_B_CAP;
    pipeline->pending_count--;
}

static emboss_pending_row_b *pending_front(emboss_pipeline *pipeline)
{
    return &pipeline->pending[pipeline->pending_head];
}

static void enqueue_row_b(emboss_pipeline *pipeline, int32_t due_x_um, uint8_t mask)
{
    size_t slot;

    if (pipeline->pending_count >= EMBOSS_ROW_B_CAP) {
        enter_fault(pipeline, BRAILLATRON_PRINT_FAULT_OVERFLOW);
        return;
    }
    slot = (pipeline->pending_head + pipeline->pending_count) % EMBOSS_ROW_B_CAP;
    pipeline->pending[slot].due_x_um = due_x_um;
    pipeline->pending[slot].mask = mask;
    pipeline->pending_count++;
}

/* Advance +X and fire any Row B whose due position is on or before the target. */
static void travel_to(emboss_pipeline *pipeline, int32_t target_x_um)
{
    while (pipeline->pending_count > 0 &&
           pending_front(pipeline)->due_x_um <= target_x_um) {
        emboss_pending_row_b row = *pending_front(pipeline);
        pop_pending(pipeline);
        do_strike(pipeline, row.mask, row.due_x_um);
        if (pipeline->state == BRAILLATRON_JOB_FAULT) {
            return;
        }
    }
    if (pipeline->state != BRAILLATRON_JOB_FAULT) {
        pipeline->x_um = target_x_um;
    }
}

static void travel_by(emboss_pipeline *pipeline, int32_t delta_um)
{
    if (delta_um < 0 || !pipeline->motors_enabled ||
        pipeline->state == BRAILLATRON_JOB_FAULT) {
        return;
    }
    travel_to(pipeline, pipeline->x_um + delta_um);
}

static void newline(emboss_pipeline *pipeline)
{
    if (!pipeline->motors_enabled || pipeline->state == BRAILLATRON_JOB_FAULT) {
        return;
    }
    travel_by(pipeline, BRAILLATRON_ROW_B_OFFSET_UM);
    if (pipeline->state == BRAILLATRON_JOB_FAULT) {
        return;
    }
    pipeline->pending_count = 0;
    pipeline->pending_head = 0;
    pipeline->x_um = 0;
    if (pipeline->motor.feed_y != NULL) {
        pipeline->motor.feed_y(pipeline->motor.user, BRAILLATRON_LINE_ADVANCE_UM);
    }
    if (pipeline->state == BRAILLATRON_JOB_FAULT) {
        return;
    }
    pipeline->y_um += BRAILLATRON_LINE_ADVANCE_UM;
    pipeline->y_line += 1;
    pipeline->line_open = 0;
    if (pipeline->state == BRAILLATRON_JOB_RECEIVING ||
        pipeline->state == BRAILLATRON_JOB_QUEUED) {
        set_state(pipeline, BRAILLATRON_JOB_PRINTING);
    }
}

static void form_feed(emboss_pipeline *pipeline)
{
    int32_t into;
    int32_t feed;
    int32_t i;

    if (pipeline->state == BRAILLATRON_JOB_FAULT) {
        return;
    }
    if (pipeline->line_open) {
        newline(pipeline);
        if (pipeline->state == BRAILLATRON_JOB_FAULT) {
            return;
        }
    }
    into = pipeline->y_line % BRAILLATRON_PAGE_LINES;
    if (into < 0) {
        into += BRAILLATRON_PAGE_LINES;
    }
    feed = (into == 0) ? BRAILLATRON_PAGE_LINES : (BRAILLATRON_PAGE_LINES - into);
    for (i = 0; i < feed; i++) {
        newline(pipeline);
        if (pipeline->state == BRAILLATRON_JOB_FAULT) {
            return;
        }
    }
    set_state(pipeline, BRAILLATRON_JOB_DONE);
    pipeline->hdr = EMBOSS_HDR_MAYBE;
    pipeline->line_len = 0;
}

static void submit_cell(emboss_pipeline *pipeline, uint8_t mask)
{
    uint8_t row_a;
    uint8_t row_b;

    if (!pipeline->motors_enabled || pipeline->state == BRAILLATRON_JOB_FAULT) {
        return;
    }
    row_a = (uint8_t)(mask & BRAILLATRON_ROW_A_DOT_MASK);
    row_b = (uint8_t)(mask & BRAILLATRON_ROW_B_DOT_MASK);
    if (pipeline->state != BRAILLATRON_JOB_PRINTING) {
        set_state(pipeline, BRAILLATRON_JOB_PRINTING);
    }
    if (row_a != 0) {
        do_strike(pipeline, row_a, pipeline->x_um);
        if (pipeline->state == BRAILLATRON_JOB_FAULT) {
            return;
        }
    }
    if (row_b != 0) {
        enqueue_row_b(pipeline, pipeline->x_um + BRAILLATRON_ROW_B_OFFSET_UM, row_b);
        if (pipeline->state == BRAILLATRON_JOB_FAULT) {
            return;
        }
    }
    pipeline->line_open = 1;
    travel_by(pipeline, BRAILLATRON_CELL_PITCH_UM);
}

static void accept_content_byte(emboss_pipeline *pipeline, uint8_t byte)
{
    uint8_t mask = 0;

    if (pipeline->state == BRAILLATRON_JOB_FAULT) {
        return;
    }
    if (pipeline->swallow_lf) {
        pipeline->swallow_lf = 0;
        if (byte == '\n') {
            return;
        }
    }
    if (byte == '\r') {
        newline(pipeline);
        pipeline->swallow_lf = 1;
        return;
    }
    if (byte == '\n') {
        newline(pipeline);
        return;
    }
    if (byte == BRAILLATRON_BRF_FORM_FEED) {
        form_feed(pipeline);
        return;
    }
    if (!braillatron_brf_ascii_to_dot_mask((char)byte, &mask)) {
        return;
    }
    submit_cell(pipeline, mask);
}

static void store_filename(emboss_pipeline *pipeline)
{
    size_t begin = sizeof(BRAILLATRON_BRF_HEADER_PREFIX) - 1;
    size_t end = pipeline->line_len;
    size_t n;

    while (begin < end && (pipeline->line[begin] == ' ' || pipeline->line[begin] == '\t')) {
        begin++;
    }
    while (end > begin && (pipeline->line[end - 1] == ' ' || pipeline->line[end - 1] == '\t')) {
        end--;
    }
    n = end - begin;
    if (n >= EMBOSS_FILENAME_CAP) {
        enter_fault(pipeline, BRAILLATRON_PRINT_FAULT_OVERFLOW);
        return;
    }
    if (n > 0) {
        memcpy(pipeline->filename, pipeline->line + begin, n);
    }
    pipeline->filename[n] = '\0';
}

static void replay_prefix_as_cells(emboss_pipeline *pipeline, int as_newline)
{
    char saved[EMBOSS_LINE_CAP];
    size_t n = pipeline->line_len;
    size_t i;

    if (n > sizeof saved) {
        enter_fault(pipeline, BRAILLATRON_PRINT_FAULT_OVERFLOW);
        return;
    }
    memcpy(saved, pipeline->line, n);
    pipeline->line_len = 0;
    pipeline->hdr = EMBOSS_HDR_BODY;
    for (i = 0; i < n; i++) {
        accept_content_byte(pipeline, (uint8_t)saved[i]);
        if (pipeline->state == BRAILLATRON_JOB_FAULT) {
            return;
        }
    }
    if (as_newline) {
        accept_content_byte(pipeline, (uint8_t)'\n');
    }
}

static void close_first_line(emboss_pipeline *pipeline, int as_newline)
{
    if (pipeline->hdr == EMBOSS_HDR_FILENAME) {
        store_filename(pipeline);
        pipeline->line_len = 0;
        pipeline->hdr = EMBOSS_HDR_BODY;
        return;
    }
    if (pipeline->hdr == EMBOSS_HDR_MAYBE) {
        replay_prefix_as_cells(pipeline, as_newline);
    }
}

static void begin_job_if_idle(emboss_pipeline *pipeline)
{
    if (pipeline->state != BRAILLATRON_JOB_IDLE &&
        pipeline->state != BRAILLATRON_JOB_DONE) {
        return;
    }
    pipeline->filename[0] = '\0';
    pipeline->line_len = 0;
    pipeline->hdr = EMBOSS_HDR_MAYBE;
    pipeline->swallow_lf = 0;
    set_state(pipeline, BRAILLATRON_JOB_RECEIVING);
}

static void on_byte(emboss_pipeline *pipeline, uint8_t byte)
{
    const char *prefix = BRAILLATRON_BRF_HEADER_PREFIX;

    if (pipeline->hdr == EMBOSS_HDR_BODY) {
        accept_content_byte(pipeline, byte);
        return;
    }
    if (byte == '\r' || byte == '\n' || byte == BRAILLATRON_BRF_FORM_FEED) {
        close_first_line(pipeline, byte == '\n' || byte == '\r');
        if (byte == '\r') {
            pipeline->swallow_lf = 1;
        }
        if (byte == BRAILLATRON_BRF_FORM_FEED &&
            pipeline->state != BRAILLATRON_JOB_FAULT) {
            form_feed(pipeline);
        }
        return;
    }
    if (pipeline->hdr == EMBOSS_HDR_MAYBE) {
        if (pipeline->line_len < sizeof(BRAILLATRON_BRF_HEADER_PREFIX) - 1 &&
            (char)byte == prefix[pipeline->line_len]) {
            pipeline->line[pipeline->line_len++] = (char)byte;
            if (prefix[pipeline->line_len] == '\0') {
                pipeline->hdr = EMBOSS_HDR_FILENAME;
            }
            return;
        }
        /* BrfCableParser also accepts a tab after "BRF1". */
        if (pipeline->line_len == 4 && byte == '\t') {
            pipeline->line[pipeline->line_len++] = '\t';
            pipeline->hdr = EMBOSS_HDR_FILENAME;
            return;
        }
        pipeline->line[pipeline->line_len++] = (char)byte;
        replay_prefix_as_cells(pipeline, 0);
        return;
    }
    if (pipeline->line_len >= EMBOSS_LINE_CAP) {
        enter_fault(pipeline, BRAILLATRON_PRINT_FAULT_OVERFLOW);
        return;
    }
    pipeline->line[pipeline->line_len++] = (char)byte;
}

void emboss_pipeline_init(emboss_pipeline *pipeline, const emboss_motor *motor,
                          char *status, size_t status_len)
{
    memset(pipeline, 0, sizeof *pipeline);
    if (motor != NULL) {
        pipeline->motor = *motor;
    }
    pipeline->status = status;
    pipeline->status_len = status_len;
    pipeline->motors_enabled = 1;
    pipeline->hdr = EMBOSS_HDR_MAYBE;
    pipeline->state = BRAILLATRON_JOB_IDLE;
    pipeline->fault = BRAILLATRON_PRINT_FAULT_NONE;
    if (pipeline->motor.set_enable != NULL) {
        pipeline->motor.set_enable(pipeline->motor.user, 1);
    }
    publish_status(pipeline);
}

void emboss_pipeline_feed(emboss_pipeline *pipeline, const uint8_t *data, size_t len)
{
    size_t i;

    if (pipeline->state == BRAILLATRON_JOB_FAULT || data == NULL || len == 0) {
        return;
    }
    for (i = 0; i < len; i++) {
        if (pipeline->state == BRAILLATRON_JOB_FAULT) {
            return;
        }
        begin_job_if_idle(pipeline);
        on_byte(pipeline, data[i]);
    }
}

void emboss_pipeline_finish(emboss_pipeline *pipeline)
{
    if (pipeline->state == BRAILLATRON_JOB_FAULT) {
        return;
    }
    if ((pipeline->state == BRAILLATRON_JOB_IDLE ||
         pipeline->state == BRAILLATRON_JOB_DONE) &&
        pipeline->hdr == EMBOSS_HDR_MAYBE && pipeline->line_len == 0) {
        return;
    }
    if (pipeline->hdr != EMBOSS_HDR_BODY) {
        close_first_line(pipeline, 0);
    }
    while (pipeline->pending_count > 0 &&
           pipeline->state != BRAILLATRON_JOB_FAULT &&
           pipeline->motors_enabled) {
        travel_to(pipeline, pending_front(pipeline)->due_x_um);
    }
    if (pipeline->state == BRAILLATRON_JOB_RECEIVING ||
        pipeline->state == BRAILLATRON_JOB_PRINTING ||
        pipeline->state == BRAILLATRON_JOB_QUEUED) {
        set_state(pipeline, BRAILLATRON_JOB_DONE);
    }
    pipeline->hdr = EMBOSS_HDR_MAYBE;
    pipeline->line_len = 0;
}

void emboss_pipeline_safety_cut(emboss_pipeline *pipeline)
{
    enter_fault(pipeline, BRAILLATRON_PRINT_FAULT_SAFETY);
}

void emboss_pipeline_fault(emboss_pipeline *pipeline, braillatron_fault_reason reason)
{
    if (reason == BRAILLATRON_PRINT_FAULT_NONE) {
        reason = BRAILLATRON_PRINT_FAULT_MOTION;
    }
    enter_fault(pipeline, reason);
}

void emboss_pipeline_clear_fault(emboss_pipeline *pipeline)
{
    if (pipeline->state != BRAILLATRON_JOB_FAULT) {
        return;
    }
    pipeline->pending_count = 0;
    pipeline->pending_head = 0;
    pipeline->line_len = 0;
    pipeline->line_open = 0;
    pipeline->hdr = EMBOSS_HDR_MAYBE;
    pipeline->swallow_lf = 0;
    pipeline->motors_enabled = 1;
    if (pipeline->motor.set_enable != NULL) {
        pipeline->motor.set_enable(pipeline->motor.user, 1);
    }
    pipeline->fault = BRAILLATRON_PRINT_FAULT_NONE;
    pipeline->state = BRAILLATRON_JOB_FAULT;
    set_state(pipeline, BRAILLATRON_JOB_IDLE);
}

int32_t emboss_pipeline_x_um(const emboss_pipeline *pipeline)
{
    return pipeline->x_um;
}

int32_t emboss_pipeline_y_um(const emboss_pipeline *pipeline)
{
    return pipeline->y_um;
}

int32_t emboss_pipeline_y_line(const emboss_pipeline *pipeline)
{
    return pipeline->y_line;
}

braillatron_job_state emboss_pipeline_state(const emboss_pipeline *pipeline)
{
    return pipeline->state;
}

int emboss_pipeline_motors_enabled(const emboss_pipeline *pipeline)
{
    return pipeline->motors_enabled;
}

const char *emboss_pipeline_filename(const emboss_pipeline *pipeline)
{
    return pipeline->filename;
}
