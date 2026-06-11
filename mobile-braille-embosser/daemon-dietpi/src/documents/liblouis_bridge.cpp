#include "liblouis_bridge.h"

#include <cctype>
#include <vector>

#ifdef BRAILLATRON_A11Y
#include <liblouis/liblouis.h>
#endif

namespace braillatron::documents {

const char *braille_table_id(BrailleTable table)
{
    switch (table) {
    case BrailleTable::UebG1:
        return "en-us-g1.ctb";
    case BrailleTable::UebG2:
        return "en-us-g2.ctb";
    case BrailleTable::Nemeth:
        return "en-us-nemeth.ctb";
    }
    return "en-us-g2.ctb";
}

BrailleTable braille_table_from_string(const std::string &name)
{
    if (name == "ueb_g1" || name == "g1") {
        return BrailleTable::UebG1;
    }
    if (name == "nemeth") {
        return BrailleTable::Nemeth;
    }
    return BrailleTable::UebG2;
}

std::string translate_forward(const std::string &plain, BrailleTable table)
{
    if (plain.empty()) {
        return {};
    }

#ifdef BRAILLATRON_A11Y
    std::vector<widechar> wide;
    wide.reserve(plain.size() + 1);
    for (unsigned char ch : plain) {
        wide.push_back(static_cast<widechar>(ch));
    }
    wide.push_back(0);

    widechar outbuf[512] = {0};
    int inlen = static_cast<int>(wide.size() - 1);
    int outlen = static_cast<int>(sizeof(outbuf) / sizeof(outbuf[0]) - 1);
    const char *tbl = braille_table_id(table);

    if (lou_translateString(tbl, wide.data(), &inlen, outbuf, &outlen, nullptr, nullptr, 0)) {
        std::string result;
        for (int i = 0; i < outlen; ++i) {
            if (outbuf[i] < 128) {
                result.push_back(static_cast<char>(outbuf[i]));
            }
        }
        return result;
    }
#endif
    (void)table;
    return plain;
}

std::string translate_backward_dots(uint8_t dot_mask, BrailleTable table)
{
    if (dot_mask == 0) {
        return " ";
    }

#ifdef BRAILLATRON_A11Y
    widechar inbuf[2] = {static_cast<widechar>(0x2800 | dot_mask), 0};
    int inlen = 1;
    widechar outbuf[16] = {0};
    int outlen = 15;
    const char *tbl = braille_table_id(table);

    if (lou_backTranslateString(tbl, inbuf, &inlen, outbuf, &outlen, nullptr, nullptr, 0)) {
        std::string result;
        for (int i = 0; i < outlen; ++i) {
            result.push_back(static_cast<char>(outbuf[i]));
        }
        return result;
    }
#endif
    (void)table;
    return {};
}

uint8_t braille_char_to_dot_mask(wchar_t braille_char)
{
    if (braille_char < 0x2800 || braille_char > 0x28FF) {
        return 0;
    }
    return static_cast<uint8_t>(braille_char & 0x3F);
}

} // namespace braillatron::documents
