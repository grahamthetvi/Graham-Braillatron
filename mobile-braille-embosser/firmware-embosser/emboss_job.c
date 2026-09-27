#include "emboss_job.h"

#include <string.h>

static int faulted(const emboss_job *job)
{
    return job->cbs.is_faulted != NULL && job->cbs.is_faulted(job->cbs.user);
}

static void emit_cell(emboss_job *job, uint8_t mask)
{
    if (faulted(job) || job->cbs.cell == NULL) {
        return;
    }
    job->cbs.cell(job->cbs.user, mask);
}

static void emit_newline(emboss_job *job)
{
    if (faulted(job) || job->cbs.newline == NULL) {
        return;
    }
    job->cbs.newline(job->cbs.user);
}

static void emit_form_feed(emboss_job *job)
{
    if (faulted(job) || job->cbs.form_feed == NULL) {
        return;
    }
    job->cbs.form_feed(job->cbs.user);
}

static void accept_content_byte(emboss_job *job, uint8_t byte)
{
    uint8_t mask = 0;

    if (faulted(job)) {
        return;
    }
    if (job->swallow_lf) {
        job->swallow_lf = 0;
        if (byte == '\n') {
            return;
        }
    }
    if (byte == '\r') {
        emit_newline(job);
        job->swallow_lf = 1;
        return;
    }
    if (byte == '\n') {
        emit_newline(job);
        return;
    }
    if (byte == BRAILLATRON_BRF_FORM_FEED) {
        emit_form_feed(job);
        return;
    }
    if (!braillatron_brf_ascii_to_dot_mask((char)byte, &mask)) {
        return;
    }
    emit_cell(job, mask);
}

static void store_filename(emboss_job *job)
{
    char name[EMBOSS_FILENAME_CAP];
    size_t begin = sizeof(BRAILLATRON_BRF_HEADER_PREFIX) - 1;
    size_t end = job->line_len;
    size_t n;

    while (begin < end && (job->line[begin] == ' ' || job->line[begin] == '\t')) {
        begin++;
    }
    while (end > begin && (job->line[end - 1] == ' ' || job->line[end - 1] == '\t')) {
        end--;
    }
    n = end - begin;
    if (n >= EMBOSS_FILENAME_CAP) {
        if (job->cbs.overflow != NULL) {
            job->cbs.overflow(job->cbs.user);
        }
        return;
    }
    if (n > 0) {
        memcpy(name, job->line + begin, n);
    }
    name[n] = '\0';
    if (job->cbs.filename != NULL) {
        job->cbs.filename(job->cbs.user, name);
    }
}

static void replay_prefix_as_cells(emboss_job *job, int as_newline)
{
    char saved[EMBOSS_LINE_CAP];
    size_t n = job->line_len;
    size_t i;

    if (n > sizeof saved) {
        if (job->cbs.overflow != NULL) {
            job->cbs.overflow(job->cbs.user);
        }
        return;
    }
    memcpy(saved, job->line, n);
    job->line_len = 0;
    job->hdr = EMBOSS_HDR_BODY;
    for (i = 0; i < n; i++) {
        accept_content_byte(job, (uint8_t)saved[i]);
        if (faulted(job)) {
            return;
        }
    }
    if (as_newline) {
        accept_content_byte(job, (uint8_t)'\n');
    }
}

static void close_first_line(emboss_job *job, int as_newline)
{
    if (job->hdr == EMBOSS_HDR_FILENAME) {
        store_filename(job);
        job->line_len = 0;
        job->hdr = EMBOSS_HDR_BODY;
        return;
    }
    if (job->hdr == EMBOSS_HDR_MAYBE) {
        replay_prefix_as_cells(job, as_newline);
    }
}

static void on_byte(emboss_job *job, uint8_t byte)
{
    const char *prefix = BRAILLATRON_BRF_HEADER_PREFIX;

    if (job->hdr == EMBOSS_HDR_BODY) {
        accept_content_byte(job, byte);
        return;
    }
    if (byte == '\r' || byte == '\n' || byte == BRAILLATRON_BRF_FORM_FEED) {
        close_first_line(job, byte == '\n' || byte == '\r');
        if (byte == '\r') {
            job->swallow_lf = 1;
        }
        if (byte == BRAILLATRON_BRF_FORM_FEED) {
            emit_form_feed(job);
        }
        return;
    }
    if (job->hdr == EMBOSS_HDR_MAYBE) {
        if (job->line_len < sizeof(BRAILLATRON_BRF_HEADER_PREFIX) - 1 &&
            (char)byte == prefix[job->line_len]) {
            job->line[job->line_len++] = (char)byte;
            if (prefix[job->line_len] == '\0') {
                job->hdr = EMBOSS_HDR_FILENAME;
            }
            return;
        }
        if (job->line_len == 4 && byte == '\t') {
            job->line[job->line_len++] = '\t';
            job->hdr = EMBOSS_HDR_FILENAME;
            return;
        }
        job->line[job->line_len++] = (char)byte;
        replay_prefix_as_cells(job, 0);
        return;
    }
    if (job->line_len >= EMBOSS_LINE_CAP) {
        if (job->cbs.overflow != NULL) {
            job->cbs.overflow(job->cbs.user);
        }
        return;
    }
    job->line[job->line_len++] = (char)byte;
}

void emboss_job_init(emboss_job *job, const emboss_job_cbs *cbs)
{
    memset(job, 0, sizeof *job);
    if (cbs != NULL) {
        job->cbs = *cbs;
    }
    job->hdr = EMBOSS_HDR_MAYBE;
}

void emboss_job_reset(emboss_job *job)
{
    emboss_job_cbs cbs = job->cbs;

    emboss_job_init(job, &cbs);
}

void emboss_job_feed(emboss_job *job, const uint8_t *data, size_t len)
{
    size_t i;

    if (data == NULL || len == 0) {
        return;
    }
    for (i = 0; i < len; i++) {
        if (faulted(job)) {
            return;
        }
        on_byte(job, data[i]);
    }
}

void emboss_job_finish(emboss_job *job)
{
    if (job->hdr != EMBOSS_HDR_BODY) {
        close_first_line(job, 0);
    }
}
