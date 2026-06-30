/*
 * Inbound frame parser (Pi -> Arduino), mirror of the Pi-side FrameParser.
 * Frames with bad sync, version, length, or CRC are silently dropped; the
 * parser resyncs on the next 0xA5 byte.
 */

#include "protocol_rx.h"

#include "protocol.h"
#include "telemetry_handler.h"
#include "watchdog.h"

#include <Arduino.h>

enum rx_state {
    RX_STATE_SYNC,
    RX_STATE_HEADER,
    RX_STATE_BODY,
};

static uint8_t g_buffer[BRAILLATRON_FRAME_MAX_SIZE];
static uint8_t g_state = RX_STATE_SYNC;
static uint8_t g_received = 0u;
static uint8_t g_expected_payload = 0u;

static void rx_reset(void)
{
    g_state = RX_STATE_SYNC;
    g_received = 0u;
    g_expected_payload = 0u;
}

static void dispatch_frame(uint32_t now_ms)
{
    const uint8_t opcode = g_buffer[2];

    switch (opcode) {
    case BRAILLATRON_OP_HEARTBEAT:
        watchdog_notify_heartbeat(now_ms);
        break;
    case BRAILLATRON_OP_TELEMETRY:
        if (g_expected_payload == (uint8_t)sizeof(braillatron_telemetry_t)) {
            braillatron_telemetry_t payload;
            for (uint8_t i = 0u; i < (uint8_t)sizeof(payload); ++i) {
                ((uint8_t *)&payload)[i] = g_buffer[(uint8_t)(BRAILLATRON_FRAME_HEADER_SIZE + i)];
            }
            telemetry_handler_apply(&payload);
        }
        break;
    default:
        break;
    }
}

static void finalize_frame(uint32_t now_ms)
{
    const uint8_t frame_len = (uint8_t)(
        BRAILLATRON_FRAME_HEADER_SIZE + g_expected_payload +
        BRAILLATRON_FRAME_CRC_SIZE);

    const uint16_t expected_crc = braillatron_crc16(
        g_buffer, (size_t)(frame_len - BRAILLATRON_FRAME_CRC_SIZE));
    const uint16_t received_crc =
        (uint16_t)g_buffer[frame_len - 2u] |
        ((uint16_t)g_buffer[frame_len - 1u] << 8);

    if (expected_crc == received_crc) {
        dispatch_frame(now_ms);
    }

    rx_reset();
}

static void rx_push_byte(uint8_t byte, uint32_t now_ms)
{
    switch (g_state) {
    case RX_STATE_SYNC:
        if (byte != BRAILLATRON_SYNC_BYTE) {
            return;
        }
        g_buffer[0] = byte;
        g_received = 1u;
        g_state = RX_STATE_HEADER;
        return;

    case RX_STATE_HEADER:
        g_buffer[g_received++] = byte;
        if (g_received < BRAILLATRON_FRAME_HEADER_SIZE) {
            return;
        }

        if (g_buffer[1] != BRAILLATRON_PROTOCOL_VERSION ||
            g_buffer[4] > BRAILLATRON_FRAME_MAX_PAYLOAD) {
            rx_reset();
            return;
        }

        g_expected_payload = g_buffer[4];
        g_state = RX_STATE_BODY;
        return;

    case RX_STATE_BODY:
    default:
        g_buffer[g_received++] = byte;
        if (g_received >= (uint8_t)(BRAILLATRON_FRAME_HEADER_SIZE +
                                    g_expected_payload +
                                    BRAILLATRON_FRAME_CRC_SIZE)) {
            finalize_frame(now_ms);
        }
        return;
    }
}

void protocol_rx_init(void)
{
    rx_reset();
}

void protocol_rx_poll(uint32_t now_ms)
{
    while (Serial.available() > 0) {
        rx_push_byte((uint8_t)Serial.read(), now_ms);
    }
}
