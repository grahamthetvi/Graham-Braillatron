#include "wikipedia_client.h"

#include "../platform/shell_util.h"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>

namespace braillatron::net {
namespace {

constexpr const char *kUserAgent = "Braillatron/1.0 (accessibility device)";
constexpr const char *kApiBase = "https://en.wikipedia.org/w/api.php";

std::string curl_fetch(const std::string &url)
{
    const std::string cmd = "curl -fsS --max-time 15 -A \"" + std::string(kUserAgent) + "\" \"" +
                            url + "\" 2>/dev/null";
    return platform::run_command(cmd);
}

std::string decode_json_string(const std::string &raw)
{
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\' || i + 1 >= raw.size()) {
            out.push_back(raw[i]);
            continue;
        }
        const char next = raw[++i];
        switch (next) {
        case '"':
        case '\\':
        case '/':
            out.push_back(next);
            break;
        case 'b':
            out.push_back('\b');
            break;
        case 'f':
            out.push_back('\f');
            break;
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;
        case 'u':
            if (i + 4 < raw.size()) {
                const std::string hex = raw.substr(i + 1, 4);
                char *end = nullptr;
                const long code = std::strtol(hex.c_str(), &end, 16);
                if (end != nullptr && *end == '\0' && code >= 0 && code <= 0x7F) {
                    out.push_back(static_cast<char>(code));
                    i += 4;
                } else {
                    out.push_back('?');
                }
            } else {
                out.push_back('?');
            }
            break;
        default:
            out.push_back(next);
            break;
        }
    }
    return out;
}

std::optional<std::string> read_json_string(const std::string &json, size_t start)
{
    if (start >= json.size() || json[start] != '"') {
        return std::nullopt;
    }

    std::string raw;
    bool escaped = false;
    for (size_t i = start + 1; i < json.size(); ++i) {
        const char ch = json[i];
        if (escaped) {
            raw.push_back('\\');
            raw.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
            return decode_json_string(raw);
        }
        raw.push_back(ch);
    }
    return std::nullopt;
}

std::optional<size_t> find_json_array_start(const std::string &json, size_t from)
{
    for (size_t i = from; i < json.size(); ++i) {
        if (json[i] == '[') {
            return i;
        }
    }
    return std::nullopt;
}

size_t advance_past_json_string(const std::string &json, size_t start)
{
    if (start >= json.size() || json[start] != '"') {
        return start;
    }

    bool escaped = false;
    for (size_t i = start + 1; i < json.size(); ++i) {
        if (escaped) {
            escaped = false;
            continue;
        }
        if (json[i] == '\\') {
            escaped = true;
            continue;
        }
        if (json[i] == '"') {
            return i + 1;
        }
    }
    return json.size();
}

size_t skip_json_array(const std::string &json, size_t start)
{
    if (start >= json.size() || json[start] != '[') {
        return start;
    }

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (size_t i = start; i < json.size(); ++i) {
        if (in_string) {
            if (escaped) {
                escaped = false;
                continue;
            }
            if (json[i] == '\\') {
                escaped = true;
                continue;
            }
            if (json[i] == '"') {
                in_string = false;
            }
            continue;
        }
        if (json[i] == '"') {
            in_string = true;
            continue;
        }
        if (json[i] == '[') {
            ++depth;
            continue;
        }
        if (json[i] == ']') {
            --depth;
            if (depth == 0) {
                return i + 1;
            }
        }
    }
    return json.size();
}

std::vector<std::string> parse_json_string_array(const std::string &json, size_t array_start)
{
    std::vector<std::string> values;
    if (array_start >= json.size() || json[array_start] != '[') {
        return values;
    }

    size_t i = array_start + 1;
    while (i < json.size()) {
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) {
            ++i;
        }
        if (i >= json.size() || json[i] == ']') {
            break;
        }
        if (json[i] != '"') {
            break;
        }
        const auto value = read_json_string(json, i);
        if (!value.has_value()) {
            break;
        }
        values.push_back(*value);

        i = advance_past_json_string(json, i);
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) {
            ++i;
        }
        if (i < json.size() && json[i] == ',') {
            ++i;
        }
    }
    return values;
}

bool is_blank_line(const std::string &line)
{
    for (char ch : line) {
        if (!std::isspace(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string url_encode(const std::string &value)
{
    static const char *hex = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size() * 3);
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded.push_back(static_cast<char>(ch));
        } else if (ch == ' ') {
            encoded.push_back('+');
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[ch >> 4]);
            encoded.push_back(hex[ch & 0x0F]);
        }
    }
    return encoded;
}

std::vector<WikipediaSearchResult> parse_opensearch_json(const std::string &json)
{
    std::vector<WikipediaSearchResult> results;
    if (json.empty()) {
        return results;
    }

    const auto first_array = find_json_array_start(json, 0);
    if (!first_array.has_value()) {
        return results;
    }

    size_t pos = *first_array + 1;
    const auto titles_array = find_json_array_start(json, pos);
    if (!titles_array.has_value()) {
        return results;
    }

    const auto titles = parse_json_string_array(json, *titles_array);
    if (titles.empty()) {
        return results;
    }

    const size_t after_titles = skip_json_array(json, *titles_array);
    const auto descriptions_array = find_json_array_start(json, after_titles);
    std::vector<std::string> descriptions;
    if (descriptions_array.has_value()) {
        descriptions = parse_json_string_array(json, *descriptions_array);
    }

    results.reserve(titles.size());
    for (size_t i = 0; i < titles.size(); ++i) {
        WikipediaSearchResult entry;
        entry.title = titles[i];
        if (i < descriptions.size()) {
            entry.description = descriptions[i];
        }
        results.push_back(std::move(entry));
    }
    return results;
}

std::optional<std::string> parse_extract_json(const std::string &json)
{
    const std::string key = "\"extract\":";
    const size_t key_pos = json.find(key);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }

    size_t i = key_pos + key.size();
    while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) {
        ++i;
    }
    if (i >= json.size() || json[i] != '"') {
        return std::nullopt;
    }
    return read_json_string(json, i);
}

std::optional<std::vector<WikipediaSearchResult>> WikipediaClient::search(const std::string &query,
                                                                            size_t limit)
{
    if (query.empty()) {
        return std::nullopt;
    }

    const std::string url = std::string(kApiBase) + "?action=opensearch&search=" +
                            url_encode(query) + "&limit=" + std::to_string(limit) + "&format=json";
    const std::string body = curl_fetch(url);
    if (body.empty()) {
        return std::nullopt;
    }

    return parse_opensearch_json(body);
}

std::optional<std::string> WikipediaClient::fetch_plaintext(const std::string &title)
{
    if (title.empty()) {
        return std::nullopt;
    }

    const std::string url = std::string(kApiBase) +
                            "?action=query&prop=extracts&explaintext=1&redirects=1&titles=" +
                            url_encode(title) + "&format=json";
    const std::string body = curl_fetch(url);
    if (body.empty()) {
        return std::nullopt;
    }
    return parse_extract_json(body);
}

std::vector<std::string> WikipediaClient::split_into_lines(const std::string &plaintext)
{
    std::vector<std::string> lines;
    if (plaintext.empty()) {
        return lines;
    }

    std::istringstream stream(plaintext);
    std::string line;
    while (std::getline(stream, line)) {
        if (!is_blank_line(line)) {
            lines.push_back(line);
        }
    }

    if (lines.empty() && !is_blank_line(plaintext)) {
        lines.push_back(plaintext);
    }
    return lines;
}

} // namespace braillatron::net
