#include "emboss_radio.h"

#include <string.h>

static void emit(emboss_radio *radio, uint8_t opcode, const uint8_t *payload, uint8_t len)
{
    uint8_t frame[16];
    size_t n;

    if (radio->tx == NULL) {
        return;
    }
    n = embosser_link_encode(frame, sizeof frame, opcode, payload, len);
    if (n == 0) {
        return;
    }
    radio->tx(radio->user, frame, n);
}

static void on_cell(void *user, uint8_t mask)
{
    uint8_t payload = mask;

    emit(user, EMBOSS_LINK_OP_CELL, &payload, 1);
}

static void on_newline(void *user)
{
    emit(user, EMBOSS_LINK_OP_NEWLINE, NULL, 0);
}

static void on_form_feed(void *user)
{
    emit(user, EMBOSS_LINK_OP_FORMFEED, NULL, 0);
}

void emboss_radio_init(emboss_radio *radio, void (*tx)(void *user, const uint8_t *frame, size_t len),
                       void *user)
{
    emboss_job_cbs cbs;

    memset(radio, 0, sizeof *radio);
    radio->tx = tx;
    radio->user = user;
    memset(&cbs, 0, sizeof cbs);
    cbs.user = radio;
    cbs.cell = on_cell;
    cbs.newline = on_newline;
    cbs.form_feed = on_form_feed;
    emboss_job_init(&radio->job, &cbs);
}

void emboss_radio_heartbeat(emboss_radio *radio)
{
    emit(radio, EMBOSS_LINK_OP_HEARTBEAT, NULL, 0);
}

void emboss_radio_feed(emboss_radio *radio, const uint8_t *data, size_t len)
{
    emboss_job_feed(&radio->job, data, len);
}

void emboss_radio_finish(emboss_radio *radio)
{
    emboss_job_finish(&radio->job);
    emit(radio, EMBOSS_LINK_OP_FINISH, NULL, 0);
}

void emboss_radio_clear_fault(emboss_radio *radio)
{
    emit(radio, EMBOSS_LINK_OP_CLEAR_FAULT, NULL, 0);
}
