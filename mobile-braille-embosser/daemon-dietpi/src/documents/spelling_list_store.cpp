#include "spelling_list_store.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace braillatron::documents {

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string &value)
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

bool parse_bool(const std::string &value)
{
    const std::string lower = trim(value);
    return lower == "1" || lower == "true" || lower == "yes" || lower == "on";
}

std::string json_escape(const std::string &value)
{
    std::string out;
    out.reserve(value.size() + 4);
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

std::string parse_json_string(const std::string &json, const std::string &key)
{
    const std::string needle = "\"" + key + "\":\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    const size_t start = pos + needle.size();
    const size_t end = json.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return json.substr(start, end - start);
}

std::vector<std::string> parse_json_string_array(const std::string &json, const std::string &key)
{
    std::vector<std::string> values;
    const std::string needle = "\"" + key + "\":[";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return values;
    }
    size_t i = pos + needle.size();
    while (i < json.size()) {
        while (i < json.size() && json[i] != '"' && json[i] != ']') {
            ++i;
        }
        if (i >= json.size() || json[i] == ']') {
            break;
        }
        ++i;
        const size_t start = i;
        while (i < json.size() && json[i] != '"') {
            if (json[i] == '\\' && i + 1 < json.size()) {
                i += 2;
                continue;
            }
            ++i;
        }
        values.push_back(json.substr(start, i - start));
        ++i;
    }
    return values;
}

std::string stem_from_path(const std::string &path)
{
    return fs::path(path).stem().string();
}

} // namespace

SpellingConfig load_spelling_config(const std::string &path)
{
    SpellingConfig config;
    std::ifstream file(path);
    if (!file.is_open()) {
        return config;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "default_list_id") {
            config.default_list_id = value;
        } else if (key == "bundled_dir") {
            config.bundled_dir = value;
        } else if (key == "custom_dir") {
            config.custom_dir = value;
        } else if (key == "session_dir") {
            config.session_dir = value;
        } else if (key == "braille_table") {
            config.braille_table = value;
        } else if (key == "sentence_tts") {
            config.sentence_tts = parse_bool(value);
        }
    }
    return config;
}

SpellingListStore::SpellingListStore(SpellingConfig config)
    : config_(std::move(config))
{
    refresh();
}

void SpellingListStore::refresh()
{
    lists_.clear();
    for (const std::string &dir : {config_.bundled_dir, config_.custom_dir}) {
        if (dir.empty()) {
            continue;
        }
        std::error_code ec;
        if (!fs::exists(dir, ec)) {
            continue;
        }
        for (const auto &entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            SpellingList list;
            if (load_json_file(entry.path().string(), &list) ||
                load_csv_file(entry.path().string(), &list)) {
                lists_.push_back(std::move(list));
            }
        }
    }
}

const SpellingList *SpellingListStore::find_list(const std::string &id) const
{
    for (const auto &list : lists_) {
        if (list.id == id) {
            return &list;
        }
    }
    return nullptr;
}

bool SpellingListStore::load_json_file(const std::string &path, SpellingList *out)
{
    std::ifstream in(path);
    if (!in.is_open() || out == nullptr) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string json = buffer.str();

    out->id = parse_json_string(json, "id");
    if (out->id.empty()) {
        out->id = stem_from_path(path);
    }
    out->name = parse_json_string(json, "name");
    if (out->name.empty()) {
        out->name = out->id;
    }
    out->words = parse_json_string_array(json, "words");
    return !out->words.empty();
}

bool SpellingListStore::load_csv_file(const std::string &path, SpellingList *out)
{
    std::ifstream in(path);
    if (!in.is_open() || out == nullptr) {
        return false;
    }

    out->id = stem_from_path(path);
    out->name = out->id;
    out->words.clear();

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line.rfind("word", 0) == 0) {
            continue;
        }
        const size_t comma = line.find(',');
        out->words.push_back(trim(comma == std::string::npos ? line : line.substr(0, comma)));
    }
    return !out->words.empty();
}

bool SpellingListStore::import_file(const std::string &path, SpellingList *out)
{
    SpellingList imported;
    if (!load_json_file(path, &imported) && !load_csv_file(path, &imported)) {
        return false;
    }
    if (out != nullptr) {
        *out = imported;
    }
    refresh();
    return true;
}

bool SpellingListStore::save_session(const SpellingSessionState &session,
                                     const std::string &timestamp) const
{
    std::error_code ec;
    fs::create_directories(config_.session_dir, ec);

    const std::string path = config_.session_dir + "/" + timestamp + ".json";
    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }

    out << "{\n"
        << "  \"list_id\":\"" << json_escape(session.list_id) << "\",\n"
        << "  \"score\":" << session.score << ",\n"
        << "  \"attempts\":" << session.attempts << ",\n"
        << "  \"missed_words\":[";
    for (size_t i = 0; i < session.missed_words.size(); ++i) {
        if (i > 0) {
            out << ',';
        }
        out << "\"" << json_escape(session.missed_words[i]) << "\"";
    }
    out << "]\n}\n";
    return true;
}

} // namespace braillatron::documents
