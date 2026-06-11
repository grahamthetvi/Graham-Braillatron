#include "wikipedia_client.h"

#include <iostream>
#include <string>

namespace {

constexpr const char *kOpensearchFixture =
    R"fixture(["Albert Einstein",["Albert Einstein","Albert Einstein House"],["German-born theoretical physicist (1879\u20131955)","Historic building"],["https://en.wikipedia.org/wiki/Albert_Einstein","https://en.wikipedia.org/wiki/Albert_Einstein_House"]])fixture";

constexpr const char *kExtractFixture =
    R"fixture({"batchcomplete":"","query":{"pages":{"736":{"pageid":736,"ns":0,"title":"Albert Einstein","extract":"Albert Einstein was a German-born theoretical physicist.\n\nHe developed the theory of relativity."}}})fixture";

bool test_opensearch_parser()
{
    const auto results = braillatron::net::parse_opensearch_json(kOpensearchFixture);
    if (results.size() != 2) {
        std::cerr << "opensearch parser: expected 2 results, got " << results.size() << "\n";
        return false;
    }
    if (results[0].title != "Albert Einstein") {
        std::cerr << "opensearch parser: unexpected first title\n";
        return false;
    }
    if (results[0].description.find("physicist") == std::string::npos) {
        std::cerr << "opensearch parser: missing description text\n";
        return false;
    }
    if (results[1].title != "Albert Einstein House") {
        std::cerr << "opensearch parser: unexpected second title\n";
        return false;
    }
    return true;
}

bool test_extract_parser()
{
    const auto extract = braillatron::net::parse_extract_json(kExtractFixture);
    if (!extract.has_value()) {
        std::cerr << "extract parser: expected value\n";
        return false;
    }
    if (extract->find("theoretical physicist") == std::string::npos) {
        std::cerr << "extract parser: missing article text\n";
        return false;
    }
    if (extract->find("theory of relativity") == std::string::npos) {
        std::cerr << "extract parser: missing second paragraph\n";
        return false;
    }
    return true;
}

bool test_url_encode()
{
    if (braillatron::net::url_encode("Albert Einstein") != "Albert+Einstein") {
        std::cerr << "url_encode: space encoding failed\n";
        return false;
    }
    if (braillatron::net::url_encode("a&b") != "a%26b") {
        std::cerr << "url_encode: ampersand encoding failed\n";
        return false;
    }
    return true;
}

bool test_split_lines()
{
    braillatron::net::WikipediaClient client;
    const auto lines = client.split_into_lines("First line.\n\nSecond line.\n\n");
    if (lines.size() != 2) {
        std::cerr << "split_into_lines: expected 2 lines, got " << lines.size() << "\n";
        return false;
    }
    if (lines[0] != "First line." || lines[1] != "Second line.") {
        std::cerr << "split_into_lines: unexpected line content\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!test_opensearch_parser() || !test_extract_parser() || !test_url_encode() ||
        !test_split_lines()) {
        return 1;
    }

    std::cout << "wikipedia self-test ok\n";
    return 0;
}
