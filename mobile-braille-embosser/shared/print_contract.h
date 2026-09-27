/*
 * Braillatron print contract — single source of truth for both products.
 *
 * Header-only plain C so the Pi daemon and a future embosser toolchain can
 * include it without a new link object. Page geometry, the North American
 * BRF table, job states, and the cable markers live here.
 *
 * The embosser streams a job. It must not require the Pi cable parser's
 * 2 MiB RAM buffer (BrfCableParser::kMaxBytes).
 *
 * Logical actuators: emboss_1 .. emboss_6 are dots 1 .. 6.
 * Bit 0 is emboss_1 (dot 1). Bit 5 is emboss_6 (dot 6).
 */

#ifndef BRAILLATRON_PRINT_CONTRACT_H
#define BRAILLATRON_PRINT_CONTRACT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Page geometry (micrometres). Matches daemon-dietpi motion_constants.h.    */
/* -------------------------------------------------------------------------- */

#define BRAILLATRON_CELL_PITCH_UM        6000   /* 6.0 mm cell pitch */
#define BRAILLATRON_ROW_B_OFFSET_UM      2500   /* 2.5 mm; dots 2, 4, 6 after Row A */
#define BRAILLATRON_LINE_ADVANCE_UM     10000   /* 10 mm line advance */
#define BRAILLATRON_PAGE_LINES             33

/* Row A punches dots 1, 3, 5 (emboss_1, emboss_3, emboss_5) at the current X. */
#define BRAILLATRON_ROW_A_DOT_MASK \
    ((uint8_t)((1u << 0) | (1u << 2) | (1u << 4)))

/* Row B punches dots 2, 4, 6 (emboss_2, emboss_4, emboss_6) after the offset. */
#define BRAILLATRON_ROW_B_DOT_MASK \
    ((uint8_t)((1u << 1) | (1u << 3) | (1u << 5)))

/* -------------------------------------------------------------------------- */
/* Job states and faults. Wire tokens are the lowercase names below.         */
/* -------------------------------------------------------------------------- */

typedef enum braillatron_job_state {
    BRAILLATRON_JOB_IDLE = 0,
    BRAILLATRON_JOB_RECEIVING,
    BRAILLATRON_JOB_QUEUED,
    BRAILLATRON_JOB_PRINTING,
    BRAILLATRON_JOB_DONE,
    BRAILLATRON_JOB_FAULT
} braillatron_job_state;

/* PRINT_ prefix: protocol.h already owns BRAILLATRON_FAULT_* for the co-processor. */
typedef enum braillatron_fault_reason {
    BRAILLATRON_PRINT_FAULT_NONE = 0,
    BRAILLATRON_PRINT_FAULT_SAFETY,
    BRAILLATRON_PRINT_FAULT_PAPER,
    BRAILLATRON_PRINT_FAULT_MOTION,
    BRAILLATRON_PRINT_FAULT_OVERFLOW
} braillatron_fault_reason;

/* -------------------------------------------------------------------------- */
/* Cable grammar. Same markers as BrfCableParser.                             */
/* -------------------------------------------------------------------------- */

#define BRAILLATRON_BRF_BAUD                 115200
#define BRAILLATRON_BRF_IDLE_COMPLETE_MS       1500
#define BRAILLATRON_BRF_HEADER_PREFIX        "BRF1 "
#define BRAILLATRON_BRF_FORM_FEED            0x0C

/* -------------------------------------------------------------------------- */
/* North American BRF: ASCII 0x20..0x5F -> dot offset.                        */
/* Copied from the table previously named kBrfToDotOffset in                  */
/* daemon-dietpi/src/documents/brf_format.cpp (Graham Braille Editor          */
/* client/src/utils/braille.ts).                                              */
/* -------------------------------------------------------------------------- */

#define BRAILLATRON_BRF_TABLE_LEN 64

static const uint8_t braillatron_brf_to_dot_offset[BRAILLATRON_BRF_TABLE_LEN] = {
    0x00, 0x2E, 0x10, 0x3C, 0x2B, 0x29, 0x2F, 0x04, /* space ! " # $ % & ' */
    0x37, 0x3E, 0x21, 0x2C, 0x20, 0x24, 0x28, 0x0C, /* ( ) * + , - . / */
    0x34, 0x02, 0x06, 0x12, 0x32, 0x22, 0x16, 0x36, /* 0 1 2 3 4 5 6 7 */
    0x26, 0x14, 0x31, 0x30, 0x23, 0x3F, 0x1C, 0x39, /* 8 9 : ; < = > ? */
    0x08, 0x01, 0x03, 0x09, 0x19, 0x11, 0x0B, 0x1B, /* @ A B C D E F G */
    0x13, 0x0A, 0x1A, 0x05, 0x07, 0x0D, 0x1D, 0x15, /* H I J K L M N O */
    0x0F, 0x1F, 0x17, 0x0E, 0x1E, 0x25, 0x27, 0x3A, /* P Q R S T U V W */
    0x2D, 0x3D, 0x35, 0x2A, 0x33, 0x3B, 0x18, 0x38  /* X Y Z [ \ ] ^ _ */
};

/*
 * Fold ASCII the same way as brf_ascii_to_dot_mask: bytes 0x60..0x7F drop
 * by 0x20 (so a-z land on A-Z), then the 64-entry table applies. Space is a
 * blank cell (mask 0, returns true). Characters outside the table return
 * false. A NULL dot_mask returns false.
 */
static inline bool braillatron_brf_ascii_to_dot_mask(char ch, uint8_t *dot_mask)
{
    unsigned char code;

    if (dot_mask == NULL) {
        return false;
    }
    code = (unsigned char)ch;
    if (code >= 0x60 && code <= 0x7F) {
        code = (unsigned char)(code - 0x20);
    }
    if (code < 0x20 || code > 0x5F) {
        return false;
    }
    *dot_mask = (uint8_t)(braillatron_brf_to_dot_offset[code - 0x20] & 0x3Fu);
    return true;
}

static inline int braillatron_status_append(char *dst, size_t cap, size_t *used,
                                            const char *src)
{
    size_t i = 0;

    while (src[i] != '\0') {
        if (*used + 1 >= cap) {
            return 0;
        }
        dst[*used] = src[i];
        *used += 1;
        i++;
    }
    return 1;
}

/*
 * Write one status line and return its length excluding the NUL, or 0 if
 * dst cannot hold the line and its NUL. A fault reason is written only when
 * state is fault: "BRFSTAT fault <reason>\n". Otherwise "BRFSTAT <state>\n".
 */
static inline size_t braillatron_print_status_line(char *dst, size_t dst_len,
                                                   braillatron_job_state state,
                                                   braillatron_fault_reason fault)
{
    const char *state_name;
    const char *fault_name;
    char line[40];
    size_t used = 0;
    size_t i;

    if (dst == NULL || dst_len == 0) {
        return 0;
    }

    switch (state) {
    case BRAILLATRON_JOB_IDLE:
        state_name = "idle";
        break;
    case BRAILLATRON_JOB_RECEIVING:
        state_name = "receiving";
        break;
    case BRAILLATRON_JOB_QUEUED:
        state_name = "queued";
        break;
    case BRAILLATRON_JOB_PRINTING:
        state_name = "printing";
        break;
    case BRAILLATRON_JOB_DONE:
        state_name = "done";
        break;
    case BRAILLATRON_JOB_FAULT:
        state_name = "fault";
        break;
    default:
        return 0;
    }

    if (!braillatron_status_append(line, sizeof line, &used, "BRFSTAT ")) {
        return 0;
    }
    if (state == BRAILLATRON_JOB_FAULT) {
        switch (fault) {
        case BRAILLATRON_PRINT_FAULT_NONE:
            fault_name = "none";
            break;
        case BRAILLATRON_PRINT_FAULT_SAFETY:
            fault_name = "safety";
            break;
        case BRAILLATRON_PRINT_FAULT_PAPER:
            fault_name = "paper";
            break;
        case BRAILLATRON_PRINT_FAULT_MOTION:
            fault_name = "motion";
            break;
        case BRAILLATRON_PRINT_FAULT_OVERFLOW:
            fault_name = "overflow";
            break;
        default:
            return 0;
        }
        if (!braillatron_status_append(line, sizeof line, &used, state_name) ||
            !braillatron_status_append(line, sizeof line, &used, " ") ||
            !braillatron_status_append(line, sizeof line, &used, fault_name)) {
            return 0;
        }
    } else if (!braillatron_status_append(line, sizeof line, &used, state_name)) {
        return 0;
    }
    if (!braillatron_status_append(line, sizeof line, &used, "\n")) {
        return 0;
    }
    if (used + 1 > dst_len) {
        return 0;
    }
    for (i = 0; i < used; i++) {
        dst[i] = line[i];
    }
    dst[used] = '\0';
    return used;
}

#ifdef __cplusplus
}
#endif

#endif /* BRAILLATRON_PRINT_CONTRACT_H */
