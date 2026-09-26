#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace braillatron::documents {

/**
 * North American Braille ASCII (BRF) codec.
 *
 * The dot map is the same table as Graham Braille Editor
 * (`client/src/utils/braille.ts`, BRF_TO_UNICODE_OFFSETS). Back-translation
 * uses that map so a `.brf` file is not fed to liblouis as computer braille.
 */

/** Fold a BRF / Unicode-braille buffer to ASCII BRF. Newlines and form feeds stay. */
std::string normalize_brf_document(const std::string &raw);

/** True when the buffer looks like contracted (Grade 2) literary BRF. */
bool is_contracted_brf(const std::string &raw);

/**
 * Liblouis sometimes back-translates a standalone dots 2-3-6 cell as "his".
 * In a quoted sentence that cell is an opening quote. Applied per line.
 */
std::string restore_ueb_open_quote_from_his(const std::string &plain);

/** Split on newlines. A form feed becomes a line break so page boundaries stay aligned. */
std::vector<std::string> split_brf_lines(const std::string &text);

struct BrfCell {
    uint8_t dot_mask = 0;
};

enum class BrfMark : uint8_t {
    Cell,
    Newline,
    FormFeed,
};

struct BrfToken {
    BrfMark mark = BrfMark::Cell;
    uint8_t dot_mask = 0;
};

/** Tokenize ASCII or Unicode BRF. Unmapped characters are dropped. */
std::vector<BrfToken> tokenize_brf(const std::string &raw);

/** True when `ch` is a North American BRF cell, including space (blank cell). */
bool brf_ascii_to_dot_mask(char ch, uint8_t *dot_mask);

} // namespace braillatron::documents
