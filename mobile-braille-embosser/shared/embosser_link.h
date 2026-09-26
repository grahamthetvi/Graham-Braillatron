/*
 * UART between the embosser's ESP32-S3 and the Spider STM32.
 *
 * ESP32-S3 receives a .brf and sends cells. The Spider steps the motors and
 * answers with status. Motors stay off until a heartbeat, and a 1000 ms gap
 * is a safety cut.
 *
 * Frame: [sync 0xA6 | opcode | len | payload | crc16-le]
 * CRC-16/CCITT-FALSE over sync through the last payload byte (braillatron_crc16).
 * This is not the co-processor frame (sync 0xA5).
 */

#ifndef BRAILLATRON_EMBOSSER_LINK_H
#define BRAILLATRON_EMBOSSER_LINK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMBOSS_LINK_SYNC 0xA6u
#define EMBOSS_LINK_MAX_PAYLOAD 8u
#define EMBOSS_LINK_HEARTBEAT_MS 1000u

/* ESP32-S3 -> Spider */
#define EMBOSS_LINK_OP_HEARTBEAT 0x01u   /* no payload */
#define EMBOSS_LINK_OP_CELL 0x02u        /* uint8 dot mask, 0 is a blank cell */
#define EMBOSS_LINK_OP_NEWLINE 0x03u     /* no payload */
#define EMBOSS_LINK_OP_FORMFEED 0x04u    /* no payload */
#define EMBOSS_LINK_OP_FINISH 0x05u      /* no payload; idle gap already elapsed */
#define EMBOSS_LINK_OP_CLEAR_FAULT 0x06u /* no payload; motors stay off until heartbeat */

/* Spider -> ESP32-S3 */
#define EMBOSS_LINK_OP_STATUS 0x07u /* uint8 job state, uint8 fault reason */

typedef struct embosser_link_parser {
    uint8_t phase;
    uint8_t opcode;
    uint8_t len;
    uint8_t got;
    uint8_t payload[EMBOSS_LINK_MAX_PAYLOAD + 2u];
    uint8_t prefix[3];
} embosser_link_parser;

/* Bytes written, or 0 if dst cannot hold the frame. */
size_t embosser_link_encode(uint8_t *dst, size_t dst_len, uint8_t opcode,
                            const uint8_t *payload, uint8_t len);

void embosser_link_parser_init(embosser_link_parser *parser);

/*
 * Feed one byte. Returns 1 and fills opcode/payload/len when a frame checks
 * out. A bad CRC is dropped and the parser resyncs.
 */
int embosser_link_parser_feed(embosser_link_parser *parser, uint8_t byte,
                              uint8_t *opcode, uint8_t *payload, uint8_t *len);

#ifdef __cplusplus
}
#endif

#endif /* BRAILLATRON_EMBOSSER_LINK_H */
