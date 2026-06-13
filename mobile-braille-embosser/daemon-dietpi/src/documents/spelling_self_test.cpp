#include "spelling_list_store.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
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

std::string temp_dir()
{
    return "/tmp/braillatron-spelling-self-test-" + std::to_string(::getpid());
}

bool write_file(const std::string &path, const std::string &contents)
{
    std::ofstream out(path);
    if (!out.is_open()) {
        return false;
    }
    out << contents;
    return true;
}

bool test_json_and_csv_import()
{
    const std::string dir = temp_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    expect_true(write_file(dir + "/grade3.json",
                           R"({"id":"grade3","name":"Grade 3","words":["apple","banana"]})"),
                "write json list");
    expect_true(write_file(dir + "/custom.csv", "word\norange\ngrape\n"), "write csv list");

    braillatron::documents::SpellingConfig config;
    config.bundled_dir = dir;
    config.custom_dir = dir + "/missing";
    braillatron::documents::SpellingListStore store(config);
    expect_true(store.lists().size() >= 2, "lists loaded");

    const auto *grade3 = store.find_list("grade3");
    expect_true(grade3 != nullptr, "grade3 list found");
    if (grade3 != nullptr) {
        expect_true(grade3->words.size() == 2, "grade3 word count");
    }

    const auto *custom = store.find_list("custom");
    expect_true(custom != nullptr, "csv list found");
    if (custom != nullptr) {
        expect_true(custom->words.size() == 2, "csv word count");
    }

    std::filesystem::remove_all(dir, ec);
    return true;
}

bool test_quiz_scoring_and_missed_queue()
{
    braillatron::documents::SpellingSessionState session;
    session.list_id = "grade3";
    session.attempts = 3;
    session.score = 2;
    session.missed_words = {"banana"};

    const std::string dir = temp_dir() + "-sessions";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    braillatron::documents::SpellingConfig config;
    config.session_dir = dir;
    braillatron::documents::SpellingListStore store(config);
    expect_true(store.save_session(session, "test-session"), "session saved");
    expect_true(std::filesystem::exists(dir + "/test-session.json"), "session file exists");

    std::ifstream in(dir + "/test-session.json");
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    expect_true(contents.find("\"score\":2") != std::string::npos, "session score saved");
    expect_true(contents.find("banana") != std::string::npos, "missed word saved");

    std::filesystem::remove_all(dir, ec);
    return true;
}

} // namespace

int main()
{
    test_json_and_csv_import();
    test_quiz_scoring_and_missed_queue();

    if (failures != 0) {
        std::cerr << failures << " spelling self-test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "spelling self-test passed\n";
    return EXIT_SUCCESS;
}
