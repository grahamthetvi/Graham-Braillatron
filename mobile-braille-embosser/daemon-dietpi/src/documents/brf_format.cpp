#include "brf_format.h"

#include <cctype>
#include <regex>

namespace braillatron::documents {

namespace {

// ASCII 0x20..0x5F → Unicode braille dot offset. Copied from
// Graham_Braille_Editor client/src/utils/braille.ts.
constexpr uint8_t kBrfToDotOffset[64] = {
    0x00, 0x2E, 0x10, 0x3C, 0x2B, 0x29, 0x2F, 0x04, // space ! " # $ % & '
    0x37, 0x3E, 0x21, 0x2C, 0x20, 0x24, 0x28, 0x0C, // ( ) * + , - . /
    0x34, 0x02, 0x06, 0x12, 0x32, 0x22, 0x16, 0x36, // 0 1 2 3 4 5 6 7
    0x26, 0x14, 0x31, 0x30, 0x23, 0x3F, 0x1C, 0x39, // 8 9 : ; < = > ?
    0x08, 0x01, 0x03, 0x09, 0x19, 0x11, 0x0B, 0x1B, // @ A B C D E F G
    0x13, 0x0A, 0x1A, 0x05, 0x07, 0x0D, 0x1D, 0x15, // H I J K L M N O
    0x0F, 0x1F, 0x17, 0x0E, 0x1E, 0x25, 0x27, 0x3A, // P Q R S T U V W
    0x2D, 0x3D, 0x35, 0x2A, 0x33, 0x3B, 0x18, 0x38  // X Y Z [ \ ] ^ _
};

char dot_offset_to_brf_ascii(uint8_t offset)
{
    for (int i = 0; i < 64; ++i) {
        if (kBrfToDotOffset[i] == (offset & 0x3F)) {
            return static_cast<char>(0x20 + i);
        }
    }
    return ' ';
}

std::string unicode_braille_to_ascii(const std::string &raw)
{
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size();) {
        const unsigned char lead = static_cast<unsigned char>(raw[i]);
        uint32_t cp = lead;
        size_t len = 1;
        if ((lead & 0x80u) == 0) {
            cp = lead;
        } else if ((lead & 0xE0u) == 0xC0u && i + 1 < raw.size()) {
            cp = (static_cast<uint32_t>(lead & 0x1Fu) << 6) |
                 static_cast<unsigned char>(raw[i + 1] & 0x3Fu);
            len = 2;
        } else if ((lead & 0xF0u) == 0xE0u && i + 2 < raw.size()) {
            cp = (static_cast<uint32_t>(lead & 0x0Fu) << 12) |
                 (static_cast<uint32_t>(static_cast<unsigned char>(raw[i + 1]) & 0x3Fu) << 6) |
                 static_cast<unsigned char>(raw[i + 2] & 0x3Fu);
            len = 3;
        } else if ((lead & 0xF8u) == 0xF0u && i + 3 < raw.size()) {
            cp = (static_cast<uint32_t>(lead & 0x07u) << 18) |
                 (static_cast<uint32_t>(static_cast<unsigned char>(raw[i + 1]) & 0x3Fu) << 12) |
                 (static_cast<uint32_t>(static_cast<unsigned char>(raw[i + 2]) & 0x3Fu) << 6) |
                 static_cast<unsigned char>(raw[i + 3] & 0x3Fu);
            len = 4;
        }
        if (cp >= 0x2800 && cp <= 0x28FF) {
            out.push_back(dot_offset_to_brf_ascii(static_cast<uint8_t>(cp - 0x2800)));
        } else if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        }
        i += len;
    }
    return out;
}

bool is_word_edge(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '"' ||
           ch == '\'';
}

bool is_word_end(char ch)
{
    return is_word_edge(ch) || ch == ',' || ch == '.' || ch == ':' || ch == ';' || ch == '!' ||
           ch == '?';
}

bool is_strong_contraction(char ch)
{
    return ch == '&' || ch == '=' || ch == '(' || ch == '?' || ch == '!';
}

bool is_consonant_word(char ch)
{
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (lower < 'b' || lower > 'z') {
        return false;
    }
    return lower != 'a' && lower != 'e' && lower != 'i' && lower != 'o' && lower != 'u';
}

bool is_group_sign(char ch)
{
    return ch == '+' || ch == '$' || ch == ']' || ch == '>' || ch == '<' || ch == '%' || ch == '\\';
}

} // namespace

bool brf_ascii_to_dot_mask(char ch, uint8_t *dot_mask)
{
    if (dot_mask == nullptr) {
        return false;
    }
    unsigned char code = static_cast<unsigned char>(ch);
    if (code >= 0x60 && code <= 0x7F) {
        code = static_cast<unsigned char>(code - 0x20);
    }
    if (code < 0x20) {
        return false;
    }
    const int index = static_cast<int>(code) - 0x20;
    if (index < 0 || index >= 64) {
        return false;
    }
    *dot_mask = static_cast<uint8_t>(kBrfToDotOffset[index] & 0x3F);
    return true;
}

std::string normalize_brf_document(const std::string &raw)
{
    std::string text = unicode_braille_to_ascii(raw);
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (ch == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                continue;
            }
            out.push_back('\n');
            continue;
        }
        out.push_back(ch);
    }
    return out;
}

bool is_contracted_brf(const std::string &raw)
{
    const std::string ascii = normalize_brf_document(raw);
    if (ascii.find_first_not_of(" \t\n\r\f") == std::string::npos) {
        return false;
    }

    int strong = 0;
    int consonant_words = 0;
    int group_signs = 0;
    for (size_t i = 0; i < ascii.size(); ++i) {
        const char ch = ascii[i];
        if (is_group_sign(ch)) {
            ++group_signs;
        }
        const bool at_start = (i == 0) || is_word_edge(ascii[i - 1]);
        if (!at_start) {
            continue;
        }
        const bool at_end = (i + 1 == ascii.size()) || is_word_end(ascii[i + 1]);
        if (!at_end) {
            continue;
        }
        if (is_strong_contraction(ch)) {
            ++strong;
        } else if (is_consonant_word(ch)) {
            ++consonant_words;
        }
    }

    if (strong >= 1) {
        return true;
    }
    if (consonant_words >= 1) {
        return true;
    }
    if (group_signs >= 2) {
        return true;
    }
    return group_signs >= 1 && consonant_words >= 1;
}

std::string restore_ueb_open_quote_from_his(const std::string &plain)
{
    if (plain.empty()) {
        return plain;
    }
    try {
        const std::regex line_re(R"(^(\s*)his\s+(.*?"\s*)$)",
                                 std::regex::icase | std::regex::optimize);
        std::string out;
        size_t start = 0;
        while (start <= plain.size()) {
            size_t end = plain.find('\n', start);
            if (end == std::string::npos) {
                end = plain.size();
            }
            const std::string line = plain.substr(start, end - start);
            std::smatch match;
            if (std::regex_match(line, match, line_re) && match.size() >= 3) {
                out += match[1].str();
                out.push_back('"');
                out += match[2].str();
            } else {
                out += line;
            }
            if (end == plain.size()) {
                break;
            }
            out.push_back('\n');
            start = end + 1;
        }
        return out;
    } catch (const std::regex_error &) {
        return plain;
    }
}

std::vector<std::string> split_brf_lines(const std::string &text)
{
    std::string normalized = text;
    for (char &ch : normalized) {
        if (ch == '\f') {
            ch = '\n';
        }
    }
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= normalized.size()) {
        size_t end = normalized.find('\n', start);
        if (end == std::string::npos) {
            end = normalized.size();
        }
        lines.push_back(normalized.substr(start, end - start));
        if (end == normalized.size()) {
            break;
        }
        start = end + 1;
    }
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    return lines;
}

std::vector<BrfToken> tokenize_brf(const std::string &raw)
{
    const std::string ascii = normalize_brf_document(raw);
    std::vector<BrfToken> tokens;
    tokens.reserve(ascii.size());
    for (char ch : ascii) {
        if (ch == '\n') {
            tokens.push_back(BrfToken {BrfMark::Newline, 0});
            continue;
        }
        if (ch == '\f') {
            tokens.push_back(BrfToken {BrfMark::FormFeed, 0});
            continue;
        }
        uint8_t mask = 0;
        if (!brf_ascii_to_dot_mask(ch, &mask)) {
            continue;
        }
        tokens.push_back(BrfToken {BrfMark::Cell, mask});
    }
    return tokens;
}

} // namespace braillatron::documents
