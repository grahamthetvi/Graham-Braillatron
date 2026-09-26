#include "embosser_link.h"

#include "protocol.h"

#include <string.h>

size_t embosser_link_encode(uint8_t *dst, size_t dst_len, uint8_t opcode,
                            const uint8_t *payload, uint8_t len)
{
    uint16_t crc;
    size_t total;

    if (dst == NULL || len > EMBOSS_LINK_MAX_PAYLOAD) {
        return 0;
    }
    if (len > 0 && payload == NULL) {
        return 0;
    }
    total = (size_t)5u + (size_t)len;
    if (dst_len < total) {
        return 0;
    }
    dst[0] = EMBOSS_LINK_SYNC;
    dst[1] = opcode;
    dst[2] = len;
    if (len > 0) {
        memcpy(dst + 3, payload, len);
    }
    crc = braillatron_crc16(dst, (size_t)3u + (size_t)len);
    dst[3 + len] = (uint8_t)(crc & 0xFFu);
    dst[4 + len] = (uint8_t)((crc >> 8) & 0xFFu);
    return total;
}

void embosser_link_parser_init(embosser_link_parser *parser)
{
    memset(parser, 0, sizeof *parser);
}

static int finish_frame(embosser_link_parser *parser, uint8_t *opcode, uint8_t *payload,
                        uint8_t *len)
{
    uint8_t raw[3 + EMBOSS_LINK_MAX_PAYLOAD];
    uint16_t expect;
    uint16_t got;
    uint8_t i;

    raw[0] = parser->prefix[0];
    raw[1] = parser->prefix[1];
    raw[2] = parser->prefix[2];
    for (i = 0; i < parser->len; i++) {
        raw[3 + i] = parser->payload[i];
    }
    expect = braillatron_crc16(raw, (size_t)3u + (size_t)parser->len);
    got = (uint16_t)parser->payload[parser->len];
    got = (uint16_t)(got | ((uint16_t)parser->payload[parser->len + 1] << 8));
    /* CRC bytes are stored past the payload in the same array. */
    if (got != expect) {
        return 0;
    }
    *opcode = parser->opcode;
    *len = parser->len;
    if (parser->len > 0 && payload != NULL) {
        memcpy(payload, parser->payload, parser->len);
    }
    return 1;
}

int embosser_link_parser_feed(embosser_link_parser *parser, uint8_t byte, uint8_t *opcode,
                              uint8_t *payload, uint8_t *len)
{
    if (parser->phase == 0) {
        if (byte != EMBOSS_LINK_SYNC) {
            return 0;
        }
        parser->prefix[0] = byte;
        parser->phase = 1;
        return 0;
    }
    if (parser->phase == 1) {
        parser->opcode = byte;
        parser->prefix[1] = byte;
        parser->phase = 2;
        return 0;
    }
    if (parser->phase == 2) {
        if (byte > EMBOSS_LINK_MAX_PAYLOAD) {
            parser->phase = (byte == EMBOSS_LINK_SYNC) ? 1 : 0;
            if (parser->phase == 1) {
                parser->prefix[0] = EMBOSS_LINK_SYNC;
            }
            return 0;
        }
        parser->len = byte;
        parser->prefix[2] = byte;
        parser->got = 0;
        parser->phase = 3;
        return 0;
    }
    /* phase 3: payload then 2 CRC bytes, stored back to back. */
    if (parser->got < parser->len + 2u) {
        parser->payload[parser->got] = byte;
        parser->got++;
    }
    if (parser->got < parser->len + 2u) {
        return 0;
    }
    parser->phase = 0;
    if (opcode == NULL || len == NULL) {
        return 0;
    }
    return finish_frame(parser, opcode, payload, len);
}
