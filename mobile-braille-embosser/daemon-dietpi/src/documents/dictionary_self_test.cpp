#include "dictionary_store.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sqlite3.h>
#include <string>
#include <unistd.h>

namespace {

int failures = 0;

void expect_true(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string fixture_path()
{
    return "/tmp/braillatron-dictionary-self-test-" + std::to_string(::getpid()) + ".sqlite";
}

bool create_fixture_db(const std::string &path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);

    sqlite3 *db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        return false;
    }

    const char *schema =
        "CREATE TABLE entries (word TEXT NOT NULL, pos TEXT, definition TEXT NOT NULL);"
        "INSERT INTO entries VALUES ('hello','noun','A greeting.');"
        "INSERT INTO entries VALUES ('hello','interjection','Used to attract attention.');"
        "INSERT INTO entries VALUES ('world','noun','The earth or human society.');";
    char *err = nullptr;
    const int rc = sqlite3_exec(db, schema, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        if (err != nullptr) {
            sqlite3_free(err);
        }
        sqlite3_close(db);
        return false;
    }
    sqlite3_close(db);
    return true;
}

bool test_lookup()
{
    const std::string path = fixture_path();
    expect_true(create_fixture_db(path), "fixture db created");

    braillatron::documents::DictionaryConfig config;
    config.db_path = path;
    config.max_definitions = 5;
    braillatron::documents::DictionaryStore store(config);
    expect_true(store.open(), "dictionary store opens");

    const auto hello = store.lookup("HELLO");
    expect_true(hello.size() == 2, "hello has two definitions");
    if (!hello.empty()) {
        expect_true(hello[0].definition.find("greeting") != std::string::npos,
                    "hello definition text");
    }

    const auto missing = store.lookup("missingword");
    expect_true(missing.empty(), "missing word returns empty");

    const auto prefixes = store.prefix_matches("hel");
    expect_true(prefixes.size() == 1, "prefix match count");
    if (!prefixes.empty()) {
        expect_true(prefixes[0] == "hello", "prefix match value");
    }

    store.close();
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return true;
}

} // namespace

int main()
{
    test_lookup();

    if (failures != 0) {
        std::cerr << failures << " dictionary self-test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "dictionary self-test passed\n";
    return EXIT_SUCCESS;
}
