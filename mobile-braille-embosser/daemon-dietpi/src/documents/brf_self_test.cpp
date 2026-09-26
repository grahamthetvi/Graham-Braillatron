#include "brf_cable.h"
#include "brf_format.h"

#include <iostream>
#include <string>

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
        std::cerr << "FAIL: " << message << " expected '" << expected << "' got '" << actual
                  << "'\n";
        ++failures;
    }
}

void test_dot_map()
{
    uint8_t mask = 0xFF;
    expect_true(braillatron::documents::brf_ascii_to_dot_mask('A', &mask), "A is a cell");
    expect_true(mask == 0x01, "A is dot 1");
    expect_true(braillatron::documents::brf_ascii_to_dot_mask('b', &mask), "lowercase b folds");
    expect_true(mask == 0x03, "B is dots 1-2");
    expect_true(braillatron::documents::brf_ascii_to_dot_mask(' ', &mask), "space is a cell");
    expect_true(mask == 0, "space is a blank cell");
    expect_true(!braillatron::documents::brf_ascii_to_dot_mask('\n', &mask), "newline is not a cell");
}

void test_unicode_round_trip_cell()
{
    const std::string ascii = braillatron::documents::normalize_brf_document("\u2801");
    expect_eq(ascii, "A", "unicode dot 1 becomes BRF A");
}

void test_contracted_and_quote()
{
    expect_true(braillatron::documents::is_contracted_brf(" & "), "standalone and-sign is grade 2");
    expect_true(!braillatron::documents::is_contracted_brf("ABC"), "letters alone are not grade 2");
    expect_eq(braillatron::documents::restore_ueb_open_quote_from_his("his hello\""), "\"hello\"",
              "standalone his before a closing quote becomes an opening quote");
    expect_eq(braillatron::documents::restore_ueb_open_quote_from_his("his book"), "his book",
              "the word his without a closing quote stays");
}

void test_parser_form_feed_and_header()
{
    braillatron::documents::BrfCableParser parser;
    const std::string bytes = "BRF1 notes.brf\nABC\n\f";
    const auto job = parser.feed(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), 1000);
    expect_true(job.has_value(), "form feed completes a job");
    if (!job.has_value()) {
        return;
    }
    expect_eq(job->filename, "notes.brf", "header filename");
    expect_eq(job->brf, "ABC\n", "header body");
    expect_true(job->accepted, "letter cells are accepted");
}

void test_parser_idle()
{
    braillatron::documents::BrfCableParser parser;
    const std::string bytes = "AB";
    expect_true(!parser.feed(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), 1000)
                     .has_value(),
                "bytes without form feed stay open");
    const auto job = parser.poll(1000 + braillatron::documents::BrfCableParser::kIdleCompleteMs);
    expect_true(job.has_value(), "idle gap completes a job");
    if (job.has_value()) {
        expect_eq(job->brf, "AB", "idle body");
        expect_true(job->filename.empty(), "no header");
    }
}

void test_parser_rejects_binary()
{
    braillatron::documents::BrfCableParser parser;
    const unsigned char bytes[] = {0x00, 0x01, 0x02, 0x03, 0x04, '\f'};
    const auto job = parser.feed(bytes, sizeof(bytes), 1000);
    expect_true(job.has_value(), "binary still ends a job");
    if (job.has_value()) {
        expect_true(!job->accepted, "binary is not Braille");
    }
}

} // namespace

int main()
{
    test_dot_map();
    test_unicode_round_trip_cell();
    test_contracted_and_quote();
    test_parser_form_feed_and_header();
    test_parser_idle();
    test_parser_rejects_binary();

    if (failures != 0) {
        std::cerr << failures << " brf self-test failure(s)\n";
        return 1;
    }
    std::cout << "brf self-test passed\n";
    return 0;
}
