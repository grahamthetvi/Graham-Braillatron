#include "emboss_pipeline.h"
#include "emboss_radio.h"
#include "emboss_spider.h"
#include "emboss_steps.h"

#include <stdio.h>
#include <string.h>

#define MAX_CMDS 64
#define WIRE_CAP 1024

typedef struct step_cmd {
    uint8_t socket;
    int32_t steps;
} step_cmd;

typedef struct recorder {
    step_cmd cmds[MAX_CMDS];
    size_t count;
    int enabled;
} recorder;

typedef struct wire {
    uint8_t bytes[WIRE_CAP];
    size_t len;
} wire;

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

static void on_step(void *user, uint8_t socket, int32_t steps)
{
    recorder *rec = user;

    if (rec->count >= MAX_CMDS) {
        fail("step recorder overflow");
        return;
    }
    rec->cmds[rec->count].socket = socket;
    rec->cmds[rec->count].steps = steps;
    rec->count++;
}

static void on_enable(void *user, int enabled)
{
    emboss_steps *steps = user;
    recorder *rec = steps->user;

    rec->enabled = enabled;
}

static void on_tx(void *user, const uint8_t *frame, size_t len)
{
    wire *out = user;

    if (out->len + len > WIRE_CAP) {
        fail("wire overflow");
        return;
    }
    memcpy(out->bytes + out->len, frame, len);
    out->len += len;
}

static emboss_axis_drive axis_drive(uint32_t rotation_um)
{
    emboss_axis_drive drive;

    drive.rotation_distance_um = rotation_um;
    drive.full_steps_per_rev = EMBOSS_STEPS_FULL_STEPS_PER_REV;
    drive.microsteps = EMBOSS_STEPS_MICROSTEPS;
    return drive;
}

static void setup(emboss_pipeline *pipeline, recorder *rec, emboss_steps *steps, emboss_radio *radio,
                  wire *forward, emboss_spider *spider, wire *back, char *status, size_t status_len)
{
    emboss_motor motor;
    emboss_axis_drive x_drive = axis_drive(EMBOSS_STEPS_X_ROTATION_UM);
    emboss_axis_drive y_drive = axis_drive(EMBOSS_STEPS_Y_ROTATION_UM);
    emboss_axis_drive punch_drive = axis_drive(EMBOSS_STEPS_PUNCH_ROTATION_UM);

    memset(rec, 0, sizeof *rec);
    rec->enabled = -1;
    memset(forward, 0, sizeof *forward);
    memset(back, 0, sizeof *back);
    emboss_steps_init(steps, &x_drive, &y_drive, &punch_drive, EMBOSS_STEPS_PUNCH_STROKE_UM, on_step,
                      rec);
    emboss_steps_as_motor(steps, &motor);
    motor.set_enable = on_enable;
    emboss_pipeline_init(pipeline, &motor, status, status_len);
    emboss_radio_init(radio, on_tx, forward);
    emboss_spider_init(spider, pipeline, on_tx, back);
}

static void pump(emboss_spider *spider, wire *forward, uint64_t now_ms)
{
    emboss_spider_rx(spider, forward->bytes, forward->len, now_ms);
    forward->len = 0;
}

static void feed_str(emboss_radio *radio, const char *text)
{
    emboss_radio_feed(radio, (const uint8_t *)text, strlen(text));
}

static void expect_seq(const recorder *rec, const step_cmd *seq, size_t n, const char *message)
{
    size_t i;

    if (rec->count != n) {
        fprintf(stderr, "FAIL: %s (got %zu commands, want %zu)\n", message, rec->count, n);
        failures++;
        return;
    }
    for (i = 0; i < n; i++) {
        if (rec->cmds[i].socket != seq[i].socket || rec->cmds[i].steps != seq[i].steps) {
            fprintf(stderr, "FAIL: %s at %zu got socket %u steps %d\n", message, i,
                    rec->cmds[i].socket, rec->cmds[i].steps);
            failures++;
            return;
        }
    }
}

static void test_step_formula(void)
{
    emboss_axis_drive x_drive = axis_drive(EMBOSS_STEPS_X_ROTATION_UM);
    emboss_axis_drive y_drive = axis_drive(EMBOSS_STEPS_Y_ROTATION_UM);
    emboss_axis_drive punch_drive = axis_drive(EMBOSS_STEPS_PUNCH_ROTATION_UM);

    expect_true(emboss_steps_from_um(6000, &x_drive) == 480, "6000 um on 40 mm/rev is 480 steps");
    expect_true(emboss_steps_from_um(2500, &x_drive) == 200, "2500 um on 40 mm/rev is 200 steps");
    expect_true(emboss_steps_from_um(3500, &x_drive) == 280, "3500 um on 40 mm/rev is 280 steps");
    expect_true(emboss_steps_from_um(10000, &y_drive) == 1600, "10000 um on 20 mm/rev is 1600 steps");
    expect_true(emboss_steps_from_um((int32_t)EMBOSS_STEPS_PUNCH_STROKE_UM, &punch_drive) == 160,
                "2000 um punch stroke is 160 steps");
    expect_true(emboss_steps_from_um(-8500, &x_drive) == -680,
                "carriage return of 8500 um is -680 steps");
}

static void test_letter_b(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    emboss_steps steps;
    emboss_radio radio;
    emboss_spider spider;
    wire forward;
    wire back;
    char status[64];
    static const step_cmd seq[] = {
        {2, 160},
        {2, -160},
        {0, 200},
        {3, 160},
        {3, -160},
        {0, 280},
    };

    setup(&pipeline, &rec, &steps, &radio, &forward, &spider, &back, status, sizeof status);
    emboss_radio_heartbeat(&radio);
    pump(&spider, &forward, 0);
    expect_true(rec.enabled == 1, "heartbeat enables motors before B");
    feed_str(&radio, "B");
    expect_true(forward.len == 0, "a lone B is held in case it is BRF1");
    emboss_radio_finish(&radio);
    pump(&spider, &forward, 20);
    expect_seq(&rec, seq, sizeof seq / sizeof seq[0], "B strikes Row A before any carriage move");
    expect_true(emboss_pipeline_x_um(&pipeline) == BRAILLATRON_CELL_PITCH_UM, "B rests one cell along");
    expect_true(emboss_pipeline_y_um(&pipeline) == 0, "B does not feed Y");
}

static void test_newline_returns_home(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    emboss_steps steps;
    emboss_radio radio;
    emboss_spider spider;
    wire forward;
    wire back;
    char status[64];
    static const step_cmd seq[] = {
        {2, 160},
        {2, -160},
        {0, 200},
        {3, 160},
        {3, -160},
        {0, 280},
        {0, 200},
        {0, -680},
        {1, 1600},
    };

    setup(&pipeline, &rec, &steps, &radio, &forward, &spider, &back, status, sizeof status);
    emboss_radio_heartbeat(&radio);
    pump(&spider, &forward, 0);
    feed_str(&radio, "B\n");
    pump(&spider, &forward, 30);
    expect_seq(&rec, seq, sizeof seq / sizeof seq[0], "newline flushes, returns home, then feeds Y");
    expect_true(emboss_pipeline_x_um(&pipeline) == 0, "newline leaves logical X at 0");
    expect_true(emboss_pipeline_y_um(&pipeline) == BRAILLATRON_LINE_ADVANCE_UM,
                "newline advances Y by 10000 um");
}

static void test_letter_a_without_paper_sensor(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    emboss_steps steps;
    emboss_radio radio;
    emboss_spider spider;
    wire forward;
    wire back;
    char status[64];
    static const step_cmd seq[] = {
        {2, 160},
        {2, -160},
        {0, 480},
    };

    setup(&pipeline, &rec, &steps, &radio, &forward, &spider, &back, status, sizeof status);
    emboss_radio_heartbeat(&radio);
    pump(&spider, &forward, 0);
    feed_str(&radio, "A");
    pump(&spider, &forward, 10);
    expect_seq(&rec, seq, sizeof seq / sizeof seq[0], "absent paper sensor still punches A");
    expect_true(rec.enabled == 1, "an absent sensor leaves motors enabled");
}

static void test_paper_absent_faults(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    emboss_steps steps;
    emboss_radio radio;
    emboss_spider spider;
    wire forward;
    wire back;
    char status[64];

    setup(&pipeline, &rec, &steps, &radio, &forward, &spider, &back, status, sizeof status);
    emboss_pipeline_set_paper_present(&pipeline, 0);
    emboss_radio_heartbeat(&radio);
    pump(&spider, &forward, 0);
    expect_true(emboss_pipeline_state(&pipeline) == BRAILLATRON_JOB_IDLE,
                "heartbeat does not paper-fault");
    expect_true(rec.enabled == 1, "heartbeat still enables motors when paper is out");
    expect_true(rec.count == 0, "heartbeat emits no steps");
    feed_str(&radio, "A");
    pump(&spider, &forward, 10);
    expect_true(rec.count == 0, "paper out emits no steps");
    expect_true(emboss_pipeline_state(&pipeline) == BRAILLATRON_JOB_FAULT, "paper out faults the job");
    expect_true(pipeline.fault == BRAILLATRON_PRINT_FAULT_PAPER, "the fault reason is paper");
    expect_true(emboss_pipeline_motors_enabled(&pipeline) == 0, "paper fault disables motors");
    expect_true(rec.enabled == 0, "paper fault calls set_enable false");
    expect_true(strcmp(status, "BRFSTAT fault paper\n") == 0, "paper fault status line");
    expect_true(emboss_pipeline_x_um(&pipeline) == 0, "paper out does not travel");
}

static void test_paper_present_punches_a(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    emboss_steps steps;
    emboss_radio radio;
    emboss_spider spider;
    wire forward;
    wire back;
    char status[64];
    static const step_cmd seq[] = {
        {2, 160},
        {2, -160},
        {0, 480},
    };

    setup(&pipeline, &rec, &steps, &radio, &forward, &spider, &back, status, sizeof status);
    emboss_pipeline_set_paper_present(&pipeline, 1);
    emboss_radio_heartbeat(&radio);
    pump(&spider, &forward, 0);
    feed_str(&radio, "A");
    pump(&spider, &forward, 10);
    expect_seq(&rec, seq, sizeof seq / sizeof seq[0], "paper present punches A then the rest of the cell");
    expect_true(emboss_pipeline_x_um(&pipeline) == BRAILLATRON_CELL_PITCH_UM, "A advances one cell");
    expect_true(rec.enabled == 1, "paper present leaves motors enabled");
}

static void test_stall_cuts_power(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    emboss_steps steps;
    emboss_radio radio;
    emboss_spider spider;
    wire forward;
    wire back;
    char status[64];

    setup(&pipeline, &rec, &steps, &radio, &forward, &spider, &back, status, sizeof status);
    emboss_radio_heartbeat(&radio);
    pump(&spider, &forward, 0);
    expect_true(rec.enabled == 1, "heartbeat enables motors before the stall");
    emboss_spider_stall(&spider);
    expect_true(spider.armed == 0, "stall disarms");
    expect_true(emboss_pipeline_state(&pipeline) == BRAILLATRON_JOB_FAULT, "stall faults the job");
    expect_true(pipeline.fault == BRAILLATRON_PRINT_FAULT_SAFETY, "stall is a safety fault");
    expect_true(emboss_pipeline_motors_enabled(&pipeline) == 0, "stall disables motors");
    expect_true(rec.enabled == 0, "stall calls set_enable false");
    expect_true(strcmp(status, "BRFSTAT fault safety\n") == 0, "stall publishes safety");
    expect_true(rec.count == 0, "stall itself emits no steps");

    feed_str(&radio, "A");
    pump(&spider, &forward, 10);
    expect_true(rec.count == 0, "a cell after stall emits no steps");
    expect_true(rec.enabled == 0, "motors stay off after the ignored cell");

    emboss_radio_heartbeat(&radio);
    pump(&spider, &forward, 20);
    feed_str(&radio, "A");
    pump(&spider, &forward, 30);
    expect_true(rec.count == 0, "a heartbeat during stall fault does not re-arm");
    expect_true(spider.armed == 0, "stall stays disarmed until clear-fault");
    expect_true(rec.enabled == 0, "motors stay off while the stall fault is held");
}

int main(void)
{
    test_step_formula();
    test_letter_b();
    test_newline_returns_home();
    test_letter_a_without_paper_sensor();
    test_paper_absent_faults();
    test_paper_present_punches_a();
    test_stall_cuts_power();
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("emboss_steps_test: ok\n");
    return 0;
}
