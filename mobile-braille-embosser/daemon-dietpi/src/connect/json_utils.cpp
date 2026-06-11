#include "json_utils.h"

#include <cctype>
#include <sstream>

namespace braillatron::connect {

namespace {

std::string trim_copy(const std::string &value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

} // namespace

std::string json_escape(const std::string &value)
{
    std::string out;
    out.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += ch;
            break;
        }
    }
    return out;
}

std::string json_get_string(const std::string &json, const std::string &key)
{
    const std::string needle = "\"" + key + "\":";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    size_t start = pos + needle.size();
    while (start < json.size() && std::isspace(static_cast<unsigned char>(json[start]))) {
        ++start;
    }
    if (start >= json.size()) {
        return {};
    }
    if (json[start] == '"') {
        ++start;
        std::string out;
        for (size_t i = start; i < json.size(); ++i) {
            if (json[i] == '\\' && i + 1 < json.size()) {
                out += json[i + 1];
                ++i;
                continue;
            }
            if (json[i] == '"') {
                break;
            }
            out += json[i];
        }
        return out;
    }
    const size_t end = json.find_first_of(",}\n", start);
    if (end == std::string::npos) {
        return trim_copy(json.substr(start));
    }
    return trim_copy(json.substr(start, end - start));
}

bool json_get_bool(const std::string &json, const std::string &key, bool default_value)
{
    const std::string needle = "\"" + key + "\":";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return default_value;
    }
    const size_t start = pos + needle.size();
    if (json.compare(start, 4, "true") == 0) {
        return true;
    }
    if (json.compare(start, 5, "false") == 0) {
        return false;
    }
    return default_value;
}

std::vector<std::string> json_split_objects(const std::string &array_json)
{
    std::vector<std::string> objects;
    size_t i = 0;
    while (i < array_json.size() && array_json[i] != '{') {
        ++i;
    }
    while (i < array_json.size()) {
        if (array_json[i] != '{') {
            ++i;
            continue;
        }
        int depth = 0;
        bool in_string = false;
        bool escape = false;
        const size_t start = i;
        for (; i < array_json.size(); ++i) {
            const char ch = array_json[i];
            if (in_string) {
                if (escape) {
                    escape = false;
                } else if (ch == '\\') {
                    escape = true;
                } else if (ch == '"') {
                    in_string = false;
                }
                continue;
            }
            if (ch == '"') {
                in_string = true;
                continue;
            }
            if (ch == '{') {
                ++depth;
            } else if (ch == '}') {
                --depth;
                if (depth == 0) {
                    objects.push_back(array_json.substr(start, i - start + 1));
                    ++i;
                    break;
                }
            }
        }
    }
    return objects;
}

} // namespace braillatron::connect
