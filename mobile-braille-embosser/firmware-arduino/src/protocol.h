/*
 * Arduino sketch copy — source of truth: ../shared/protocol.h (keep in sync).
 * Braillatron inter-processor protocol — single source of truth.
 *
 * Shared by firmware-arduino (ATmega32U4) and daemon-dietpi (Orange Pi 3B).
 * All wire fields are little-endian. Payload structs use __attribute__((packed)).
 *
 * Frame layout: [sync | version | opcode | sequence_id | payload_len | payload | crc16]
 * CRC16-CCITT-FALSE over bytes from sync through last payload byte.
 */

#ifndef BRAILLATRON_PROTOCOL_H
#define BRAILLATRON_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Protocol constants                                                         */
/* -------------------------------------------------------------------------- */

#define BRAILLATRON_SYNC_BYTE           0xA5u
#define BRAILLATRON_PROTOCOL_VERSION    1u

#define BRAILLATRON_FRAME_HEADER_SIZE   5u
#define BRAILLATRON_FRAME_CRC_SIZE      2u
#define BRAILLATRON_FRAME_MIN_SIZE      7u
#define BRAILLATRON_FRAME_MAX_PAYLOAD   32u
#define BRAILLATRON_FRAME_MAX_SIZE      39u

#define BRAILLATRON_FRAME_TOTAL_SIZE(payload_len) \
    ((size_t)(BRAILLATRON_FRAME_HEADER_SIZE) + (size_t)(payload_len) + \
     (size_t)(BRAILLATRON_FRAME_CRC_SIZE))

#define BRAILLATRON_TELEMETRY_UNKNOWN       255u
#define BRAILLATRON_TELEMETRY_UNKNOWN_S8    127

/* -------------------------------------------------------------------------- */
/* Opcodes                                                                    */
/* -------------------------------------------------------------------------- */

typedef enum {
    BRAILLATRON_OP_KEYBOARD_MATRIX = 0x01u, /* Arduino -> Pi; edge-triggered */
    BRAILLATRON_OP_TELEMETRY       = 0x02u, /* Pi -> Arduino; periodic/alert */
    BRAILLATRON_OP_SAFETY          = 0x03u, /* Bidirectional fault broadcast */
    BRAILLATRON_OP_HEARTBEAT       = 0x04u, /* Reserved; no v1 payload struct */
    BRAILLATRON_OP_ACK_NACK        = 0x05u, /* Reserved; no v1 payload struct */
} braillatron_opcode_t;

/* -------------------------------------------------------------------------- */
/* Frame header                                                               */
/* -------------------------------------------------------------------------- */

typedef struct __attribute__((packed)) {
    uint8_t sync;
    uint8_t version;
    uint8_t opcode;
    uint8_t sequence_id;
    uint8_t payload_len;
} braillatron_frame_header_t;

/* -------------------------------------------------------------------------- */
/* Keyboard matrix payload (2 bytes)                                          */
/* -------------------------------------------------------------------------- */
/*
 * 4x4 NKRO matrix. key_state bit N = row (N/4), col (N%4), 1 = pressed.
 * Arduino transmits BRAILLATRON_OP_KEYBOARD_MATRIX only on debounced edge
 * change (8 ms integrator). Pi applies a 40 ms chord integration window.
 */

#define BRAILLATRON_KEY_DOT_1      (1u << 0)
#define BRAILLATRON_KEY_DOT_2      (1u << 1)
#define BRAILLATRON_KEY_DOT_3      (1u << 2)
#define BRAILLATRON_KEY_DOT_4      (1u << 3)
#define BRAILLATRON_KEY_DOT_5      (1u << 4)
#define BRAILLATRON_KEY_DOT_6      (1u << 5)
#define BRAILLATRON_KEY_DPAD_UP    (1u << 6)
#define BRAILLATRON_KEY_DPAD_DOWN  (1u << 7)
#define BRAILLATRON_KEY_BACKSPACE  (1u << 8)
#define BRAILLATRON_KEY_ENTER      (1u << 9)
#define BRAILLATRON_KEY_SHIFT_TTS  (1u << 10)
#define BRAILLATRON_KEY_SPEECH     (1u << 11)
#define BRAILLATRON_KEY_MENU       (1u << 12)
/* bits 13-15 reserved; must be 0 */

typedef struct __attribute__((packed)) {
    uint16_t key_state;
} braillatron_keyboard_matrix_t;

/* -------------------------------------------------------------------------- */
/* Telemetry payload (3 bytes)                                                  */
/* -------------------------------------------------------------------------- */
/*
 * Pi-origin relay: LTC2944 fuel gauge, limit sensors, motion policy.
 */

#define BRAILLATRON_LIMIT_PAPER_EDGE        (1u << 0) /* TCRT5000 */
#define BRAILLATRON_LIMIT_Y_HOME            (1u << 1) /* TCST2103 */
#define BRAILLATRON_LIMIT_MOTION_BLOCKED    (1u << 2) /* SOC < 5% or policy */
#define BRAILLATRON_LIMIT_BATTERY_CRITICAL  (1u << 3) /* LTC2944 shutdown band */
/* bits 4-7 reserved */

typedef struct __attribute__((packed)) {
    uint8_t battery_percent; /* 0-100; BRAILLATRON_TELEMETRY_UNKNOWN if unread */
    int8_t  temperature_c; /* deg C; BRAILLATRON_TELEMETRY_UNKNOWN_S8 if unread */
    uint8_t limit_status;    /* BRAILLATRON_LIMIT_* flags, OR'd */
} braillatron_telemetry_t;

/* -------------------------------------------------------------------------- */
/* Safety / error broadcast payload (5 bytes)                                   */
/* -------------------------------------------------------------------------- */
/*
 * detail conventions (fault-specific):
 *   FREEFALL:         0 = event asserted
 *   BATTERY_CRITICAL: lower byte = last known SOC %
 *   WATCHDOG_TIMEOUT: ms since last host heartbeat (lower 16 bits)
 */

typedef enum {
    BRAILLATRON_FAULT_NONE             = 0x00u,
    BRAILLATRON_FAULT_FREEFALL         = 0x01u, /* MPU6050 INT0 */
    BRAILLATRON_FAULT_WATCHDOG_TIMEOUT = 0x02u,
    BRAILLATRON_FAULT_COMMS_LOSS       = 0x03u,
    BRAILLATRON_FAULT_BATTERY_CRITICAL = 0x04u, /* SOC < 5% */
    BRAILLATRON_FAULT_THERMAL          = 0x05u,
    BRAILLATRON_FAULT_ESTOP            = 0x06u,
    BRAILLATRON_FAULT_MOTION_BLOCKED   = 0x07u,
} braillatron_fault_code_t;

typedef enum {
    BRAILLATRON_SEVERITY_INFO     = 0u,
    BRAILLATRON_SEVERITY_WARNING  = 1u,
    BRAILLATRON_SEVERITY_CRITICAL = 2u,
    BRAILLATRON_SEVERITY_LATCHED  = 3u, /* requires explicit clear */
} braillatron_severity_t;

#define BRAILLATRON_SOURCE_ARDUINO  0x01u
#define BRAILLATRON_SOURCE_DAEMON     0x02u

typedef struct __attribute__((packed)) {
    uint8_t  fault_code; /* braillatron_fault_code_t */
    uint8_t  severity;   /* braillatron_severity_t */
    uint16_t detail;
    uint8_t  source;     /* BRAILLATRON_SOURCE_* */
} braillatron_safety_broadcast_t;

/* -------------------------------------------------------------------------- */
/* Size guards                                                                */
/* -------------------------------------------------------------------------- */

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(braillatron_frame_header_t) == 5,
               "braillatron_frame_header_t must be 5 bytes");
_Static_assert(sizeof(braillatron_keyboard_matrix_t) == 2,
               "braillatron_keyboard_matrix_t must be 2 bytes");
_Static_assert(sizeof(braillatron_telemetry_t) == 3,
               "braillatron_telemetry_t must be 3 bytes");
_Static_assert(sizeof(braillatron_safety_broadcast_t) == 5,
               "braillatron_safety_broadcast_t must be 5 bytes");
#endif

/* -------------------------------------------------------------------------- */
/* CRC16-CCITT-FALSE (poly 0x1021, init 0xFFFF)                               */
/* -------------------------------------------------------------------------- */

uint16_t braillatron_crc16(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* BRAILLATRON_PROTOCOL_H */
