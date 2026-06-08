#include "protocol_tx.h"

#include "pins.h"

#include "protocol.h"

#include <Arduino.h>
#include <string.h>

static uint8_t g_sequence_id = 0u;

static void protocol_tx_frame(uint8_t opcode, const void *payload, uint8_t payload_len)
{
    uint8_t frame[BRAILLATRON_FRAME_MAX_SIZE];
    const size_t frame_len = BRAILLATRON_FRAME_TOTAL_SIZE(payload_len);

    braillatron_frame_header_t *header = (braillatron_frame_header_t *)frame;
    header->sync = BRAILLATRON_SYNC_BYTE;
    header->version = BRAILLATRON_PROTOCOL_VERSION;
    header->opcode = opcode;
    header->sequence_id = g_sequence_id++;
    header->payload_len = payload_len;

    if (payload_len > 0u && payload != nullptr) {
        memcpy(frame + BRAILLATRON_FRAME_HEADER_SIZE, payload, payload_len);
    }

    const size_t crc_offset = BRAILLATRON_FRAME_HEADER_SIZE + payload_len;
    const uint16_t crc = braillatron_crc16(frame, crc_offset);
    frame[crc_offset] = (uint8_t)(crc & 0xFFu);
    frame[crc_offset + 1u] = (uint8_t)((crc >> 8) & 0xFFu);

    Serial.write(frame, frame_len);
}

void protocol_tx_init(void)
{
    Serial.begin(UART_BAUD_RATE);
}

void protocol_tx_keyboard_matrix(uint16_t key_state)
{
    braillatron_keyboard_matrix_t payload;
    payload.key_state = key_state;
    protocol_tx_frame(
        (uint8_t)BRAILLATRON_OP_KEYBOARD_MATRIX,
        &payload,
        (uint8_t)sizeof(payload));
}

void protocol_tx_safety(uint8_t fault_code, uint8_t severity, uint16_t detail)
{
    braillatron_safety_broadcast_t payload;
    payload.fault_code = fault_code;
    payload.severity = severity;
    payload.detail = detail;
    payload.source = BRAILLATRON_SOURCE_ARDUINO;
    protocol_tx_frame(
        (uint8_t)BRAILLATRON_OP_SAFETY,
        &payload,
        (uint8_t)sizeof(payload));
}
