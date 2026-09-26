#include "emboss_spider.h"

#include <string.h>

static void emit_status(emboss_spider *spider)
{
    uint8_t payload[2];
    uint8_t frame[16];
    size_t n;
    braillatron_job_state state;
    braillatron_fault_reason fault;

    if (spider->tx == NULL || spider->pipeline == NULL) {
        return;
    }
    state = emboss_pipeline_state(spider->pipeline);
    fault = spider->pipeline->fault;
    if (spider->sent_valid && spider->sent_state == state && spider->sent_fault == fault) {
        return;
    }
    payload[0] = (uint8_t)state;
    payload[1] = (uint8_t)fault;
    n = embosser_link_encode(frame, sizeof frame, EMBOSS_LINK_OP_STATUS, payload, 2);
    if (n == 0) {
        return;
    }
    spider->sent_state = state;
    spider->sent_fault = fault;
    spider->sent_valid = 1;
    spider->tx(spider->user, frame, n);
}

static void arm(emboss_spider *spider, uint64_t now_ms)
{
    spider->armed = 1;
    spider->last_heartbeat_ms = now_ms;
    emboss_pipeline_set_armed(spider->pipeline, 1);
}

static void handle(emboss_spider *spider, uint8_t opcode, const uint8_t *payload, uint8_t len,
                   uint64_t now_ms)
{
    switch (opcode) {
    case EMBOSS_LINK_OP_HEARTBEAT:
        if (emboss_pipeline_state(spider->pipeline) != BRAILLATRON_JOB_FAULT) {
            arm(spider, now_ms);
        } else {
            spider->last_heartbeat_ms = now_ms;
        }
        break;
    case EMBOSS_LINK_OP_CELL:
        if (!spider->armed || len != 1 || payload == NULL) {
            break;
        }
        emboss_pipeline_cell(spider->pipeline, payload[0]);
        break;
    case EMBOSS_LINK_OP_NEWLINE:
        if (!spider->armed) {
            break;
        }
        emboss_pipeline_newline(spider->pipeline);
        break;
    case EMBOSS_LINK_OP_FORMFEED:
        if (!spider->armed) {
            break;
        }
        emboss_pipeline_form_feed(spider->pipeline);
        break;
    case EMBOSS_LINK_OP_FINISH:
        if (!spider->armed) {
            break;
        }
        emboss_pipeline_finish(spider->pipeline);
        break;
    case EMBOSS_LINK_OP_CLEAR_FAULT:
        emboss_pipeline_clear_fault_held(spider->pipeline);
        spider->armed = 0;
        break;
    default:
        break;
    }
    emit_status(spider);
}

void emboss_spider_init(emboss_spider *spider, emboss_pipeline *pipeline,
                        void (*tx)(void *user, const uint8_t *frame, size_t len), void *user)
{
    memset(spider, 0, sizeof *spider);
    spider->pipeline = pipeline;
    spider->tx = tx;
    spider->user = user;
    embosser_link_parser_init(&spider->parser);
    if (pipeline != NULL) {
        emboss_pipeline_set_armed(pipeline, 0);
    }
}

void emboss_spider_rx(emboss_spider *spider, const uint8_t *data, size_t len, uint64_t now_ms)
{
    size_t i;

    if (data == NULL) {
        return;
    }
    for (i = 0; i < len; i++) {
        uint8_t opcode = 0;
        uint8_t payload[EMBOSS_LINK_MAX_PAYLOAD];
        uint8_t payload_len = 0;

        if (!embosser_link_parser_feed(&spider->parser, data[i], &opcode, payload,
                                       &payload_len)) {
            continue;
        }
        handle(spider, opcode, payload, payload_len, now_ms);
    }
}

void emboss_spider_poll(emboss_spider *spider, uint64_t now_ms)
{
    if (!spider->armed || spider->pipeline == NULL) {
        return;
    }
    if (emboss_pipeline_state(spider->pipeline) == BRAILLATRON_JOB_FAULT) {
        return;
    }
    if (now_ms - spider->last_heartbeat_ms < EMBOSS_LINK_HEARTBEAT_MS) {
        return;
    }
    spider->armed = 0;
    emboss_pipeline_safety_cut(spider->pipeline);
    emit_status(spider);
}
