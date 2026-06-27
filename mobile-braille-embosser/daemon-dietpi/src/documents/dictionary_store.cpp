#include "dictionary_store.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sqlite3.h>

namespace braillatron::documents {

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

} // namespace

DictionaryConfig load_dictionary_config(const std::string &path)
{
    DictionaryConfig config;
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
        if (key == "db_path") {
            config.db_path = value;
        } else if (key == "max_definitions") {
            config.max_definitions = std::max(1, std::atoi(value.c_str()));
        } else if (key == "emboss_enabled") {
            config.emboss_enabled = parse_bool(value);
        }
    }
    return config;
}

DictionaryStore::DictionaryStore(DictionaryConfig config)
    : config_(std::move(config))
{
}

bool DictionaryStore::open()
{
    close();
    if (config_.db_path.empty()) {
        return false;
    }
    sqlite3 *db = nullptr;
    const std::string uri = "file:" + config_.db_path + "?immutable=1";
    if (sqlite3_open_v2(uri.c_str(), &db,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_URI | SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        if (db != nullptr) {
            sqlite3_close(db);
        }
        return false;
    }
    db_ = db;
    return true;
}

void DictionaryStore::close()
{
    if (db_ != nullptr) {
        sqlite3_close(static_cast<sqlite3 *>(db_));
        db_ = nullptr;
    }
}

std::vector<DictionaryEntry> DictionaryStore::lookup(const std::string &word) const
{
    std::vector<DictionaryEntry> entries;
    if (db_ == nullptr || word.empty()) {
        return entries;
    }

    sqlite3 *db = static_cast<sqlite3 *>(db_);
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT word, pos, definition FROM entries WHERE word = ? COLLATE NOCASE "
        "ORDER BY rowid LIMIT ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return entries;
    }

    sqlite3_bind_text(stmt, 1, word.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, config_.max_definitions);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DictionaryEntry entry;
        if (const char *value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0))) {
            entry.word = value;
        }
        if (const char *value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1))) {
            entry.part_of_speech = value;
        }
        if (const char *value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2))) {
            entry.definition = value;
        }
        entries.push_back(std::move(entry));
    }
    sqlite3_finalize(stmt);
    return entries;
}

std::vector<std::string> DictionaryStore::prefix_matches(const std::string &prefix,
                                                         size_t limit) const
{
    std::vector<std::string> matches;
    if (db_ == nullptr || prefix.empty()) {
        return matches;
    }

    sqlite3 *db = static_cast<sqlite3 *>(db_);
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT DISTINCT word FROM entries WHERE word LIKE ? COLLATE NOCASE "
        "ORDER BY word LIMIT ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return matches;
    }

    const std::string pattern = prefix + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, static_cast<int>(limit));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (const char *value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0))) {
            matches.emplace_back(value);
        }
    }
    sqlite3_finalize(stmt);
    return matches;
}

} // namespace braillatron::documents
