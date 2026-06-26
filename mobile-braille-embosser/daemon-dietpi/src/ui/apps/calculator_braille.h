#pragma once

#include <cstdint>
#include <optional>

extern "C" {
#include "protocol.h"
}

namespace braillatron::ui {

/*
 * Computer-braille (ASCII) dot patterns for Calculator numeric entry.
 * Bypasses liblouis UEB G2 so digits do not require a number-sign prefix.
 */
inline std::optional<char> calculator_char_from_dot_mask(uint8_t dot_mask)
{
    switch (dot_mask) {
    case BRAILLATRON_KEY_DOT_1:
        return '1';
    case BRAILLATRON_KEY_DOT_1 | BRAILLATRON_KEY_DOT_2:
        return '2';
    case BRAILLATRON_KEY_DOT_1 | BRAILLATRON_KEY_DOT_4:
        return '3';
    case BRAILLATRON_KEY_DOT_1 | BRAILLATRON_KEY_DOT_4 | BRAILLATRON_KEY_DOT_5:
        return '4';
    case BRAILLATRON_KEY_DOT_1 | BRAILLATRON_KEY_DOT_5:
        return '5';
    case BRAILLATRON_KEY_DOT_1 | BRAILLATRON_KEY_DOT_2 | BRAILLATRON_KEY_DOT_4:
        return '6';
    case BRAILLATRON_KEY_DOT_1 | BRAILLATRON_KEY_DOT_2 | BRAILLATRON_KEY_DOT_4 |
        BRAILLATRON_KEY_DOT_5:
        return '7';
    case BRAILLATRON_KEY_DOT_1 | BRAILLATRON_KEY_DOT_2 | BRAILLATRON_KEY_DOT_5:
        return '8';
    case BRAILLATRON_KEY_DOT_2 | BRAILLATRON_KEY_DOT_4:
        return '9';
    case BRAILLATRON_KEY_DOT_2 | BRAILLATRON_KEY_DOT_4 | BRAILLATRON_KEY_DOT_5:
        return '0';
    case BRAILLATRON_KEY_DOT_3 | BRAILLATRON_KEY_DOT_4 | BRAILLATRON_KEY_DOT_6:
        return '+';
    case BRAILLATRON_KEY_DOT_3 | BRAILLATRON_KEY_DOT_6:
        return '-';
    case BRAILLATRON_KEY_DOT_3 | BRAILLATRON_KEY_DOT_5:
        return '*';
    case BRAILLATRON_KEY_DOT_3 | BRAILLATRON_KEY_DOT_4:
        return '/';
    case BRAILLATRON_KEY_DOT_4:
        return '.';
    case BRAILLATRON_KEY_DOT_2 | BRAILLATRON_KEY_DOT_3 | BRAILLATRON_KEY_DOT_5 |
        BRAILLATRON_KEY_DOT_6:
        return '(';
    case BRAILLATRON_KEY_DOT_2 | BRAILLATRON_KEY_DOT_3 | BRAILLATRON_KEY_DOT_5:
        return ')';
    default:
        return std::nullopt;
    }
}

} // namespace braillatron::ui
