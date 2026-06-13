#include "gmail_backend.h"

#include "connect_config.h"
#include "json_utils.h"

#include <cstdlib>
#include <filesystem>
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

void expect_eq(const std::string &actual, const std::string &expected, const char *message)
{
    if (actual != expected) {
        std::cerr << "FAIL: " << message << " (got '" << actual << "', expected '" << expected
                  << "')\n";
        ++failures;
    }
}

std::string temp_dir()
{
    const char *env = std::getenv("TMPDIR");
    const std::string base = (env != nullptr && env[0] != '\0') ? env : "/tmp";
    return base + "/braillatron-gmail-self-test-" + std::to_string(::getpid());
}

const char *kMessageFixture = R"({
  "id": "msg123",
  "threadId": "thread456",
  "snippet": "Hello preview",
  "payload": {
    "mimeType": "multipart/alternative",
    "headers": [
      {"name": "From", "value": "alice@example.com"},
      {"name": "Subject", "value": "Test subject"}
    ],
    "parts": [
      {
        "mimeType": "text/plain",
        "body": {
          "data": "SGVsbG8gd29ybGQ="
        }
      }
    ]
  }
})";

bool test_header_and_body_parsing()
{
    const std::string from =
        braillatron::connect::GmailBackend::header_from_message(kMessageFixture, "From");
    const std::string subject =
        braillatron::connect::GmailBackend::header_from_message(kMessageFixture, "Subject");
    const std::string body =
        braillatron::connect::GmailBackend::extract_plain_body(kMessageFixture);

    expect_eq(from, "alice@example.com", "from header");
    expect_eq(subject, "Test subject", "subject header");
    expect_eq(body, "Hello world", "plain body");
    return true;
}

bool test_inbox_entry_format()
{
    const std::string entry =
        braillatron::connect::GmailBackend::format_inbox_entry(kMessageFixture);
    expect_true(entry.find("msg123") != std::string::npos, "entry id");
    expect_true(entry.find("alice@example.com") != std::string::npos, "entry from");
    expect_true(entry.find("Test subject") != std::string::npos, "entry subject");
    return true;
}

bool test_rfc2822_and_base64()
{
    const std::string rfc = braillatron::connect::GmailBackend::build_rfc2822(
        "bob@example.com", "Hi", "Body text");
    expect_true(rfc.find("To: bob@example.com") != std::string::npos, "rfc to");
    expect_true(rfc.find("Subject: Hi") != std::string::npos, "rfc subject");
    expect_true(rfc.find("Body text") != std::string::npos, "rfc body");

    const std::string encoded = braillatron::connect::GmailBackend::base64url_encode("test");
    expect_eq(encoded, "dGVzdA", "base64url");
    return true;
}

bool test_format_message_brf_lines()
{
    const auto lines = braillatron::connect::GmailBackend::format_message_brf_lines(
        "alice@example.com", "Hello", "Line one\nLine two");
    expect_true(lines.size() >= 4, "brf line count");
    expect_eq(lines[0], "From: alice@example.com", "brf from line");
    expect_eq(lines[1], "Subject: Hello", "brf subject line");
    expect_eq(lines[2], "", "brf separator");
    expect_eq(lines[3], "Line one", "brf body line 1");
    expect_eq(lines[4], "Line two", "brf body line 2");
    return true;
}

bool test_export_filename()
{
    const std::string filename =
        braillatron::connect::GmailBackend::export_filename("Team Update!");
    expect_true(filename.find("team-update-") == 0, "filename stem");
    expect_true(filename.size() > 4 && filename.substr(filename.size() - 4) == ".brf", "brf suffix");
    return true;
}

bool test_link_status_unlinked()
{
    braillatron::connect::GmailConfig config;
    config.credentials_dir = temp_dir();
    config.client_id_path = config.credentials_dir + "/client_id";
    config.token_path = config.credentials_dir + "/token.json";
    braillatron::connect::GmailBackend backend(config, nullptr);
    const std::string status = backend.link_status();
    expect_true(braillatron::connect::json_get_bool(status, "ok", false), "link_status ok");
    expect_true(!braillatron::connect::json_get_bool(status, "linked", true), "not linked");

    std::error_code ec;
    std::filesystem::remove_all(config.credentials_dir, ec);
    return true;
}

} // namespace

int main()
{
    test_header_and_body_parsing();
    test_inbox_entry_format();
    test_rfc2822_and_base64();
    test_format_message_brf_lines();
    test_export_filename();
    test_link_status_unlinked();

    if (failures > 0) {
        std::cerr << failures << " gmail self-test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "gmail self-test passed\n";
    return EXIT_SUCCESS;
}
