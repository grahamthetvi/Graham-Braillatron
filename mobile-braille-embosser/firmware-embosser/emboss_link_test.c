#include "emboss_pipeline.h"
#include "emboss_radio.h"
#include "emboss_spider.h"

#include <stdio.h>
#include <string.h>

#define MAX_STRIKES 32
#define WIRE_CAP 1024
#define MAX_STATUS 16

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

typedef struct wire {
    uint8_t bytes[WIRE_CAP];
    size_t len;
} wire;

typedef struct status_evt {
    uint8_t state;
    uint8_t fault;
} status_evt;

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

static void setup(emboss_pipeline *pipeline, recorder *rec, emboss_radio *radio, wire *forward,
                  emboss_spider *spider, wire *back, char *status, size_t status_len)
{
    emboss_motor motor;

    memset(rec, 0, sizeof *rec);
    rec->enabled = -1;
    memset(forward, 0, sizeof *forward);
    memset(back, 0, sizeof *back);
    memset(&motor, 0, sizeof motor);
    motor.user = rec;
    motor.strike = on_strike;
    motor.feed_y = on_feed_y;
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

static size_t parse_status(const wire *back, status_evt *out, size_t cap)
{
    embosser_link_parser parser;
    size_t i;
    size_t n = 0;

    embosser_link_parser_init(&parser);
    for (i = 0; i < back->len; i++) {
        uint8_t opcode = 0;
        uint8_t payload[EMBOSS_LINK_MAX_PAYLOAD];
        uint8_t payload_len = 0;

        if (!embosser_link_parser_feed(&parser, back->bytes[i], &opcode, payload, &payload_len)) {
            continue;
        }
        if (opcode != EMBOSS_LINK_OP_STATUS || payload_len != 2) {
            fail("spider sent a frame that is not a status");
            continue;
        }
        if (n >= cap) {
            fail("status recorder overflow");
            continue;
        }
        out[n].state = payload[0];
        out[n].fault = payload[1];
        n++;
    }
    return n;
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

static int feed_parser(embosser_link_parser *parser, const uint8_t *frame, size_t len,
                       uint8_t *opcode, uint8_t *payload, uint8_t *payload_len)
{
    size_t i;
    int got = 0;

    for (i = 0; i < len; i++) {
        if (embosser_link_parser_feed(parser, frame[i], opcode, payload, payload_len)) {
            got = 1;
        }
    }
    return got;
}

static void test_roundtrip(void)
{
    embosser_link_parser parser;
    uint8_t frame[16];
    uint8_t opcode = 0;
    uint8_t payload[EMBOSS_LINK_MAX_PAYLOAD];
    uint8_t payload_len = 0;
    uint8_t cell = 0x15;
    size_t n;
    uint8_t good[16];
    size_t good_len;

    embosser_link_parser_init(&parser);
    n = embosser_link_encode(frame, sizeof frame, EMBOSS_LINK_OP_CELL, &cell, 1);
    expect_true(n == 6, "cell frame is sync opcode len mask crc16");
    expect_true(feed_parser(&parser, frame, n, &opcode, payload, &payload_len),
                "a good cell frame parses");
    expect_true(opcode == EMBOSS_LINK_OP_CELL && payload_len == 1 && payload[0] == 0x15,
                "cell payload survives the round trip");

    frame[n - 1] ^= 0x01;
    embosser_link_parser_init(&parser);
    expect_true(!feed_parser(&parser, frame, n, &opcode, payload, &payload_len),
                "a flipped CRC byte is dropped");

    good_len = embosser_link_encode(good, sizeof good, EMBOSS_LINK_OP_HEARTBEAT, NULL, 0);
    expect_true(good_len == 5, "heartbeat frame is five bytes");
    expect_true(feed_parser(&parser, good, good_len, &opcode, payload, &payload_len),
                "the next good frame still parses after a bad CRC");
    expect_true(opcode == EMBOSS_LINK_OP_HEARTBEAT && payload_len == 0,
                "the recovered frame is a heartbeat");
}

static void test_cell_without_heartbeat(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    emboss_radio radio;
    emboss_spider spider;
    wire forward;
    wire back;
    char status[64];

    setup(&pipeline, &rec, &radio, &forward, &spider, &back, status, sizeof status);
    expect_true(rec.enabled == 0, "spider init leaves motors off");
    feed_str(&radio, "A");
    expect_true(forward.len > 0, "the radio still emits the cell");
    pump(&spider, &forward, 0);
    expect_true(rec.count == 0, "a cell before heartbeat does not strike");
    expect_true(emboss_pipeline_motors_enabled(&pipeline) == 0, "motors stay disabled");
    expect_true(emboss_pipeline_x_um(&pipeline) == 0, "a ignored cell does not travel");
}

static void test_letter_a(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    emboss_radio radio;
    emboss_spider spider;
    wire forward;
    wire back;
    char status[64];
    status_evt events[MAX_STATUS];
    size_t n;

    setup(&pipeline, &rec, &radio, &forward, &spider, &back, status, sizeof status);
    emboss_radio_heartbeat(&radio);
    pump(&spider, &forward, 0);
    expect_true(rec.enabled == 1, "heartbeat enables motors");
    n = parse_status(&back, events, MAX_STATUS);
    expect_true(n == 1 && events[0].state == BRAILLATRON_JOB_IDLE &&
                    events[0].fault == BRAILLATRON_PRINT_FAULT_NONE,
                "heartbeat reports idle");
    back.len = 0;

    feed_str(&radio, "A");
    pump(&spider, &forward, 10);
    expect_true(rec.count == 1, "A is one strike");
    expect_strike(&rec, 0, 0x01, 0, "A is dot 1 at x 0");
    expect_true(emboss_pipeline_x_um(&pipeline) == BRAILLATRON_CELL_PITCH_UM, "A advances one cell");
    n = parse_status(&back, events, MAX_STATUS);
    expect_true(n == 1 && events[0].state == BRAILLATRON_JOB_PRINTING &&
                    events[0].fault == BRAILLATRON_PRINT_FAULT_NONE,
                "a cell reports printing");
}

static void test_letter_b_needs_finish(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    emboss_radio radio;
    emboss_spider spider;
    wire forward;
    wire back;
    char status[64];

    setup(&pipeline, &rec, &radio, &forward, &spider, &back, status, sizeof status);
    emboss_radio_heartbeat(&radio);
    pump(&spider, &forward, 0);
    forward.len = 0;

    feed_str(&radio, "B");
    expect_true(forward.len == 0, "a lone B is held in case it is BRF1");
    emboss_radio_finish(&radio);
    pump(&spider, &forward, 20);
    expect_true(rec.count == 2, "B is two strikes once the job finishes");
    expect_strike(&rec, 0, 0x01, 0, "B row A at x 0");
    expect_strike(&rec, 1, 0x02, BRAILLATRON_ROW_B_OFFSET_UM, "B row B at 2500");
    expect_true(emboss_pipeline_state(&pipeline) == BRAILLATRON_JOB_DONE, "finish reports done");
}

static void test_space_is_a_blank_cell(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    emboss_radio radio;
    emboss_spider spider;
    wire forward;
    wire back;
    char status[64];

    setup(&pipeline, &rec, &radio, &forward, &spider, &back, status, sizeof status);
    emboss_radio_heartbeat(&radio);
    pump(&spider, &forward, 0);
    feed_str(&radio, "A ");
    pump(&spider, &forward, 30);
    expect_true(rec.count == 1, "a space adds no strike");
    expect_true(emboss_pipeline_x_um(&pipeline) == 2 * BRAILLATRON_CELL_PITCH_UM,
                "a space still advances one cell");
}

static void test_heartbeat_gap_cuts_power(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    emboss_radio radio;
    emboss_spider spider;
    wire forward;
    wire back;
    char status[64];
    status_evt events[MAX_STATUS];
    size_t n;
    size_t strikes;

    setup(&pipeline, &rec, &radio, &forward, &spider, &back, status, sizeof status);
    emboss_radio_heartbeat(&radio);
    pump(&spider, &forward, 0);
    back.len = 0;

    emboss_spider_poll(&spider, EMBOSS_LINK_HEARTBEAT_MS - 1u);
    expect_true(emboss_pipeline_state(&pipeline) == BRAILLATRON_JOB_IDLE,
                "a gap under 1000 ms stays armed");
    expect_true(rec.enabled == 1, "motors stay on inside the window");

    emboss_spider_poll(&spider, EMBOSS_LINK_HEARTBEAT_MS);
    expect_true(emboss_pipeline_state(&pipeline) == BRAILLATRON_JOB_FAULT, "1000 ms is a safety cut");
    expect_true(emboss_pipeline_motors_enabled(&pipeline) == 0, "the cut disables motors");
    expect_true(rec.enabled == 0, "the cut calls set_enable false");
    expect_true(strcmp(status, "BRFSTAT fault safety\n") == 0, "the cut publishes safety");
    n = parse_status(&back, events, MAX_STATUS);
    expect_true(n == 1 && events[0].state == BRAILLATRON_JOB_FAULT &&
                    events[0].fault == BRAILLATRON_PRINT_FAULT_SAFETY,
                "the cut reports fault safety");

    strikes = rec.count;
    feed_str(&radio, "A");
    pump(&spider, &forward, EMBOSS_LINK_HEARTBEAT_MS + 10u);
    expect_true(rec.count == strikes, "a cell after the cut is ignored");

    emboss_radio_heartbeat(&radio);
    pump(&spider, &forward, EMBOSS_LINK_HEARTBEAT_MS + 20u);
    feed_str(&radio, "A");
    pump(&spider, &forward, EMBOSS_LINK_HEARTBEAT_MS + 30u);
    expect_true(rec.count == strikes, "a heartbeat during fault does not re-arm");
    expect_true(rec.enabled == 0, "motors stay off while faulted");
}

static void test_header_form_feed(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    emboss_radio radio;
    emboss_spider spider;
    wire forward;
    wire back;
    char status[64];
    status_evt events[MAX_STATUS];
    size_t n;

    setup(&pipeline, &rec, &radio, &forward, &spider, &back, status, sizeof status);
    emboss_radio_heartbeat(&radio);
    pump(&spider, &forward, 0);
    back.len = 0;

    feed_str(&radio, "BRF1 notes.brf\nA\f");
    pump(&spider, &forward, 40);
    expect_true(rec.count == 1, "the header is not punched");
    expect_strike(&rec, 0, 0x01, 0, "the body cell is A");
    expect_true(emboss_pipeline_y_line(&pipeline) == BRAILLATRON_PAGE_LINES,
                "form feed ejects a page");
    expect_true(rec.y_fed_um == BRAILLATRON_PAGE_LINES * BRAILLATRON_LINE_ADVANCE_UM,
                "form feed travels 33 lines");
    expect_true(emboss_pipeline_state(&pipeline) == BRAILLATRON_JOB_DONE, "form feed ends the job");
    n = parse_status(&back, events, MAX_STATUS);
    expect_true(n >= 1 && events[n - 1].state == BRAILLATRON_JOB_DONE &&
                    events[n - 1].fault == BRAILLATRON_PRINT_FAULT_NONE,
                "form feed reports done");
    expect_true(events[0].state == BRAILLATRON_JOB_PRINTING, "the cell reports printing first");
}

static void test_clear_fault_stays_dark(void)
{
    emboss_pipeline pipeline;
    recorder rec;
    emboss_radio radio;
    emboss_spider spider;
    wire forward;
    wire back;
    char status[64];
    status_evt events[MAX_STATUS];
    size_t n;

    setup(&pipeline, &rec, &radio, &forward, &spider, &back, status, sizeof status);
    emboss_radio_heartbeat(&radio);
    pump(&spider, &forward, 0);
    feed_str(&radio, "A");
    pump(&spider, &forward, 10);
    emboss_spider_poll(&spider, 10u + EMBOSS_LINK_HEARTBEAT_MS);
    expect_true(rec.count == 1, "A struck before the gap");
    back.len = 0;

    emboss_radio_clear_fault(&radio);
    pump(&spider, &forward, 2000);
    expect_true(emboss_pipeline_state(&pipeline) == BRAILLATRON_JOB_IDLE, "clear leaves idle");
    expect_true(emboss_pipeline_motors_enabled(&pipeline) == 0, "clear keeps motors off");
    expect_true(rec.enabled == 0, "clear does not pulse enable");
    n = parse_status(&back, events, MAX_STATUS);
    expect_true(n == 1 && events[0].state == BRAILLATRON_JOB_IDLE &&
                    events[0].fault == BRAILLATRON_PRINT_FAULT_NONE,
                "clear reports idle");

    feed_str(&radio, "A");
    pump(&spider, &forward, 2010);
    expect_true(rec.count == 1, "a cell after clear and before heartbeat does not strike");

    emboss_radio_heartbeat(&radio);
    pump(&spider, &forward, 2020);
    expect_true(rec.enabled == 1, "the next heartbeat enables motors");
    feed_str(&radio, "A");
    pump(&spider, &forward, 2030);
    expect_true(rec.count == 2, "a cell after the new heartbeat strikes");
}

int main(void)
{
    test_roundtrip();
    test_cell_without_heartbeat();
    test_letter_a();
    test_letter_b_needs_finish();
    test_space_is_a_blank_cell();
    test_heartbeat_gap_cuts_power();
    test_header_form_feed();
    test_clear_fault_stays_dark();
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("emboss_link_test: ok\n");
    return 0;
}
