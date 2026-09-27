#include "emboss_pipeline.h"

#include <stdio.h>
#include <string.h>

#define MAX_STRIKES 32

typedef struct strike {
    uint8_t mask;
    int32_t x_um;
} strike;

typedef struct recorder {
    strike strikes[MAX_STRIKES];
    size_t count;
    int32_t y_fed_um;
    int enabled;
} recorder;

static int failures = 0;

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    failures++;
}

static void expect_true(int condition, const char *message)
{
    if (!condition) {
        fail(message);
    }
}

static void on_strike(void *user, uint8_t dot_mask, int32_t x_um)
{
    recorder *rec = user;

    if (rec->count >= MAX_STRIKES) {
        fail("strike recorder overflow");
        return;
    }
    rec->strikes[rec->count].mask = dot_mask;
    rec->strikes[rec->count].x_um = x_um;
    rec->count++;
}

static void on_feed_y(void *user, int32_t delta_um)
{
    recorder *rec = user;
    rec->y_fed_um += delta_um;
}

static void on_enable(void *user, int enabled)
{
    recorder *rec = user;
    rec->enabled = enabled;
}

static void setup(emboss_pipeline *pipeline, recorder *rec, char *status, size_t status_len)
{
    emboss_motor motor;

    memset(rec, 0, sizeof *rec);
    rec->enabled = -1;
    memset(&motor, 0, sizeof motor);
    motor.user = rec;
    motor.strike = on_strike;
    motor.feed_y = on_feed_y;
    motor.set_enable = on_enable;
    emboss_pipeline_init(pipeline, &motor, status, status_len);
}

static void feed_str(emboss_pipeline *pipeline, const char *text)
{
    emboss_pipeline_feed(pipeline, (const uint8_t *)text, strlen(text));
}

static void expect_strike(const recorder *rec, size_t index, uint8_t mask, int32_t x_um,
                          const char *message)
{
    if (index >= rec->count) {
        fprintf(stderr, "FAIL: %s (only %zu strikes)\n", message, rec->count);
        failures++;
        return;
    }
    if (rec->strikes[index].mask != mask || rec->strikes[index].x_um != x_um) {
        fprintf(stderr, "FAIL: %s got mask 0x%02x at %d um\n", message,
                rec->strikes[index].mask, rec->strikes[index].x_um);
        failures++;
    }
}

static void test_contract_helper(void)
{
    uint8_t mask = 0xFF;
    char printing[32];
    char fault_line[32];
    char tiny[4];
    size_t n;

    expect_true(braillatron_brf_ascii_to_dot_mask('A', &mask), "A is a cell");
    expect_true(mask == 0x01, "A is dot 1");
    expect_true(braillatron_brf_ascii_to_dot_mask('b', &mask), "lowercase b folds");
    expect_true(mask == 0x03, "b folds to dots 1-2");
    expect_true(braillatron_brf_ascii_to_dot_mask(' ', &mask), "space is a cell");
    expect_true(mask == 0, "space is a blank cell");
    expect_true(!braillatron_brf_ascii_to_dot_mask('\n', &mask), "newline is outside the table");

    n = braillatron_print_status_line(printing, sizeof printing, BRAILLATRON_JOB_PRINTING,
                                      BRAILLATRON_PRINT_FAULT_PAPER);
    expect_true(n == strlen("BRFSTAT printing\n") && strcmp(printing, "BRFSTAT printing\n") == 0,
                "printing status omits a fault token");
    n = braillatron_print_status_line(fault_line, sizeof fault_line, BRAILLATRON_JOB_FAULT,
                                      BRAILLATRON_PRINT_FAULT_PAPER);
    expect_true(n == strlen("BRFSTAT fault paper\n") &&
                    strcmp(fault_line, "BRFSTAT fault paper\n") == 0,
                "paper fault status");
    n = braillatron_print_status_line(tiny, sizeof tiny, BRAILLATRON_JOB_IDLE,
                                      BRAILLATRON_PRINT_FAULT_NONE);
    expect_true(n == 0, "short status buffer returns 0");
}

static void test_letter_a(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    char status[64];

    setup(&pipeline, &rec, status, sizeof status);
    feed_str(&pipeline, "A");
    expect_true(rec.count == 1, "A is one Row A strike");
    expect_strike(&rec, 0, 0x01, 0, "A Row A at x 0");
    expect_true(emboss_pipeline_x_um(&pipeline) == BRAILLATRON_CELL_PITCH_UM,
                "A advances one cell");
    expect_true(emboss_pipeline_y_um(&pipeline) == 0, "A does not feed Y");
}

static void test_letter_b(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    char status[64];

    /*
     * A lone "B" is still a possible "BRF1 " prefix, so finish() commits it.
     * The cell's own 6000 um advance passes the 2500 um Row B point, and the
     * strike is reported at absolute X = 2500, not at the end of the cell.
     */
    setup(&pipeline, &rec, status, sizeof status);
    feed_str(&pipeline, "B");
    emboss_pipeline_finish(&pipeline);
    expect_true(rec.count == 2, "B is Row A plus Row B");
    expect_strike(&rec, 0, 0x01, 0, "B Row A at x 0");
    expect_strike(&rec, 1, 0x02, BRAILLATRON_ROW_B_OFFSET_UM, "B Row B at 2500 um");
    expect_true(emboss_pipeline_x_um(&pipeline) == BRAILLATRON_CELL_PITCH_UM,
                "B rests at the next cell");
    expect_true(emboss_pipeline_state(&pipeline) == BRAILLATRON_JOB_DONE, "finish closes B");
}

static void test_b_then_space(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    char status[64];

    /* Space breaks the header prefix and is the following cell. Row B of B
     * has already fired at 2500 um, partway through the advance to x 6000. */
    setup(&pipeline, &rec, status, sizeof status);
    feed_str(&pipeline, "B ");
    expect_true(rec.count == 2, "space adds no strike after B");
    expect_strike(&rec, 0, 0x01, 0, "B Row A before the space");
    expect_strike(&rec, 1, 0x02, BRAILLATRON_ROW_B_OFFSET_UM, "B Row B during the advance");
    expect_true(emboss_pipeline_x_um(&pipeline) == 2 * BRAILLATRON_CELL_PITCH_UM,
                "space advances a second cell");
}

static void test_space(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    char status[64];

    setup(&pipeline, &rec, status, sizeof status);
    feed_str(&pipeline, " ");
    expect_true(rec.count == 0, "space does not strike");
    expect_true(emboss_pipeline_x_um(&pipeline) == BRAILLATRON_CELL_PITCH_UM,
                "space advances one cell");
    expect_true(emboss_pipeline_y_um(&pipeline) == 0, "space does not feed Y");
}

static void test_newline_flushes_row_b(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    char status[64];

    setup(&pipeline, &rec, status, sizeof status);
    feed_str(&pipeline, "B\n");
    expect_true(rec.count == 2, "newline does not add a strike");
    expect_strike(&rec, 0, 0x01, 0, "Row A before the line feed");
    expect_strike(&rec, 1, 0x02, BRAILLATRON_ROW_B_OFFSET_UM, "Row B flushed by travel");
    expect_true(emboss_pipeline_x_um(&pipeline) == 0, "newline returns X to 0");
    expect_true(emboss_pipeline_y_um(&pipeline) == BRAILLATRON_LINE_ADVANCE_UM,
                "newline advances Y by 10000 um");
    expect_true(rec.y_fed_um == BRAILLATRON_LINE_ADVANCE_UM, "feed_y saw one line");
}

static void test_header_and_form_feed(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    char status[64];

    setup(&pipeline, &rec, status, sizeof status);
    feed_str(&pipeline, "BRF1 notes.brf\nA\f");
    expect_true(rec.count == 1, "header text is not punched");
    expect_strike(&rec, 0, 0x01, 0, "body A is the only strike");
    expect_true(strcmp(emboss_pipeline_filename(&pipeline), "notes.brf") == 0,
                "header filename is recorded");
    expect_true(emboss_pipeline_x_um(&pipeline) == 0, "form feed returns the carriage");
    expect_true(emboss_pipeline_y_line(&pipeline) == BRAILLATRON_PAGE_LINES,
                "form feed lands on the next page");
    expect_true(emboss_pipeline_y_um(&pipeline) ==
                    BRAILLATRON_PAGE_LINES * BRAILLATRON_LINE_ADVANCE_UM,
                "form feed distance is 33 lines");
    expect_true(strcmp(status, "BRFSTAT done\n") == 0, "form feed completes the job");
}

static void test_form_feed_at_page_boundary(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    char status[64];

    /* Already on a page boundary with no open line: feed a full 33 lines. */
    setup(&pipeline, &rec, status, sizeof status);
    feed_str(&pipeline, "\f");
    expect_true(rec.count == 0, "a bare form feed does not strike");
    expect_true(emboss_pipeline_y_line(&pipeline) == BRAILLATRON_PAGE_LINES,
                "boundary form feed is 33 lines");
    expect_true(rec.y_fed_um == BRAILLATRON_PAGE_LINES * BRAILLATRON_LINE_ADVANCE_UM,
                "boundary form feed distance");
}

static void test_tab_header(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    char status[64];

    setup(&pipeline, &rec, status, sizeof status);
    feed_str(&pipeline, "BRF1\tnotes.brf\nA");
    expect_true(rec.count == 1, "tab header is not punched");
    expect_strike(&rec, 0, 0x01, 0, "tab-header body is A");
    expect_true(strcmp(emboss_pipeline_filename(&pipeline), "notes.brf") == 0,
                "tab header filename");
}

static void test_printing_status(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    char status[64];

    setup(&pipeline, &rec, status, sizeof status);
    expect_true(strcmp(status, "BRFSTAT idle\n") == 0, "init publishes idle");
    feed_str(&pipeline, "A");
    expect_true(strcmp(status, "BRFSTAT printing\n") == 0, "a cell publishes printing");
    expect_true(emboss_pipeline_state(&pipeline) == BRAILLATRON_JOB_PRINTING,
                "state is printing");
}

static void test_paper_fault_status(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    char status[64];

    setup(&pipeline, &rec, status, sizeof status);
    emboss_pipeline_fault(&pipeline, BRAILLATRON_PRINT_FAULT_PAPER);
    expect_true(strcmp(status, "BRFSTAT fault paper\n") == 0, "paper fault status line");
    expect_true(emboss_pipeline_motors_enabled(&pipeline) == 0, "paper fault disables motors");
    expect_true(rec.enabled == 0, "paper fault calls set_enable false");
}

static void test_safety_cut(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    char status[64];
    size_t strikes_before;
    int32_t x_before;

    setup(&pipeline, &rec, status, sizeof status);
    feed_str(&pipeline, "A");
    strikes_before = rec.count;
    x_before = emboss_pipeline_x_um(&pipeline);
    emboss_pipeline_safety_cut(&pipeline);
    feed_str(&pipeline, "A");
    expect_true(rec.enabled == 0, "safety cut calls set_enable false");
    expect_true(emboss_pipeline_motors_enabled(&pipeline) == 0, "motors stay disabled");
    expect_true(strcmp(status, "BRFSTAT fault safety\n") == 0, "safety status line");
    expect_true(rec.count == strikes_before, "a following cell is not struck");
    expect_true(emboss_pipeline_x_um(&pipeline) == x_before, "a following cell does not travel");

    emboss_pipeline_clear_fault(&pipeline);
    expect_true(rec.enabled == 1, "clear re-enables motors");
    feed_str(&pipeline, "A");
    expect_true(rec.count == strikes_before + 1, "a cell after clear is struck");
}

int main(void)
{
    test_contract_helper();
    test_letter_a();
    test_letter_b();
    test_b_then_space();
    test_space();
    test_newline_flushes_row_b();
    test_header_and_form_feed();
    test_form_feed_at_page_boundary();
    test_tab_header();
    test_printing_status();
    test_paper_fault_status();
    test_safety_cut();
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("emboss_pipeline_test: ok\n");
    return 0;
}
