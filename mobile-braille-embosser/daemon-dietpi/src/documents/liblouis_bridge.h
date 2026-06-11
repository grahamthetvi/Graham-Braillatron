#pragma once

#include <cstdint>
#include <string>

namespace braillatron::documents {

enum class BrailleTable {
    UebG1,
    UebG2,
    Nemeth,
};

const char *braille_table_id(BrailleTable table);
BrailleTable braille_table_from_string(const std::string &name);

std::string translate_forward(const std::string &plain, BrailleTable table);
std::string translate_backward_dots(uint8_t dot_mask, BrailleTable table);

uint8_t braille_char_to_dot_mask(wchar_t braille_char);

} // namespace braillatron::documents
