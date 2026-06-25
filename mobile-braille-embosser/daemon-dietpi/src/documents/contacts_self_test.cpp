#include "contacts_store.h"

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
    return "/tmp/braillatron-contacts-self-test-" + std::to_string(::getpid());
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

bool test_csv_import_round_trip()
{
    const std::string dir = temp_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir + "/import", ec);

    expect_true(write_file(dir + "/import/sample.csv",
                           "name,phone,email,organization\n"
                           "Ada Lovelace,555-0100,ada@example.com,Analytical Engines\n"
                           "Grace Hopper,555-0200,grace@example.com,US Navy\n"),
                "write csv import");

    braillatron::documents::ContactsConfig config;
    config.store_path = dir + "/contacts.json";
    config.import_dir = dir + "/import";
    braillatron::documents::ContactsStore store(config);
    store.refresh();

    expect_true(store.contacts().size() == 2, "csv import count");
    const auto matches = store.search("grace");
    expect_true(matches.size() == 1, "search finds grace");
    if (!matches.empty()) {
        expect_true(matches[0].organization == "US Navy", "organization imported");
        expect_true(!matches[0].phones.empty(), "phone imported");
    }

    expect_true(store.save(), "contacts saved");
    expect_true(std::filesystem::exists(dir + "/contacts.json"), "contacts file exists");
    expect_true(std::filesystem::exists(dir + "/import/processed/sample.csv"),
                "csv moved to processed");

    braillatron::documents::ContactsStore reloaded(config);
    reloaded.load();
    expect_true(reloaded.contacts().size() == 2, "reload count");
    const auto *ada = reloaded.find_by_id(reloaded.search("ada")[0].id);
    expect_true(ada != nullptr, "ada found after reload");
    if (ada != nullptr) {
        expect_true(ada->emails.size() == 1, "email count after reload");
        expect_true(ada->emails[0] == "ada@example.com", "email value after reload");
    }

    std::filesystem::remove_all(dir, ec);
    return true;
}

bool test_vcard_import_round_trip()
{
    const std::string dir = temp_dir() + "-vcard";
    std::error_code ec;
    std::filesystem::create_directories(dir + "/import", ec);

    expect_true(write_file(dir + "/import/alice.vcf",
                           "BEGIN:VCARD\n"
                           "VERSION:3.0\n"
                           "FN:Alice Example\n"
                           "TEL;TYPE=CELL:555-0300\n"
                           "EMAIL:alice@example.com\n"
                           "ORG:Example Corp\n"
                           "NOTE:Met at conference\n"
                           "END:VCARD\n"),
                "write vcard import");

    braillatron::documents::ContactsConfig config;
    config.store_path = dir + "/contacts.json";
    config.import_dir = dir + "/import";
    braillatron::documents::ContactsStore store(config);
    store.refresh();

    expect_true(store.contacts().size() == 1, "vcard import count");
    const auto matches = store.search("alice");
    expect_true(matches.size() == 1, "search finds alice");
    if (!matches.empty()) {
        expect_true(matches[0].phones.size() == 1, "vcard phone count");
        expect_true(matches[0].emails.size() == 1, "vcard email count");
        expect_true(matches[0].organization == "Example Corp", "vcard organization");
        expect_true(matches[0].notes.find("conference") != std::string::npos, "vcard notes");
    }

    const std::string card = store.format_card(matches[0]);
    expect_true(card.find("Alice Example") != std::string::npos, "card has name");
    expect_true(card.find("555-0300") != std::string::npos, "card has phone");

    std::filesystem::remove_all(dir, ec);
    return true;
}

bool test_add_contact_round_trip()
{
    const std::string dir = temp_dir() + "-add";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    braillatron::documents::ContactsConfig config;
    config.store_path = dir + "/contacts.json";
    config.import_dir = dir + "/import";
    braillatron::documents::ContactsStore store(config);

    expect_true(store.add_contact("Addison Graham", "555-1234"), "add contact saved");
    expect_true(store.contacts().size() == 1, "add contact count");
    const auto matches = store.search("addison");
    expect_true(matches.size() == 1, "add contact searchable");
    if (!matches.empty()) {
        expect_true(matches[0].phones.size() == 1, "add contact phone count");
        expect_true(matches[0].phones[0] == "555-1234", "add contact phone value");
    }

    expect_true(store.add_contact("", "555-0000") == false, "empty name rejected");

    braillatron::documents::ContactsStore reloaded(config);
    reloaded.load();
    expect_true(reloaded.contacts().size() == 1, "add contact reload count");
    expect_true(!reloaded.search("addison").empty(), "add contact persists after reload");

    std::filesystem::remove_all(dir, ec);
    return true;
}

} // namespace

int main()
{
    test_csv_import_round_trip();
    test_vcard_import_round_trip();
    test_add_contact_round_trip();

    if (failures != 0) {
        std::cerr << failures << " contacts self-test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "contacts self-test passed\n";
    return EXIT_SUCCESS;
}
