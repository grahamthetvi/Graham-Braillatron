#include "brf_format.h"

#include "print_contract.h"

#include <cctype>
#include <regex>

namespace braillatron::documents {

namespace {

// North American table lives in print_contract.h (braillatron_brf_to_dot_offset).

char dot_offset_to_brf_ascii(uint8_t offset)
{
    for (int i = 0; i < BRAILLATRON_BRF_TABLE_LEN; ++i) {
        if (braillatron_brf_to_dot_offset[i] == (offset & 0x3F)) {
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
    return braillatron_brf_ascii_to_dot_mask(ch, dot_mask);
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
