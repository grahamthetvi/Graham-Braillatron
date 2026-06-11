#pragma once

#include <optional>
#include <string>
#include <vector>

namespace braillatron::net {

struct WikipediaSearchResult {
    std::string title;
    std::string description;
};

class WikipediaClient {
public:
    std::optional<std::vector<WikipediaSearchResult>> search(const std::string &query,
                                                             size_t limit = 5);
    std::optional<std::string> fetch_plaintext(const std::string &title);
    std::vector<std::string> split_into_lines(const std::string &plaintext);
};

// Exposed for offline unit tests.
std::vector<WikipediaSearchResult> parse_opensearch_json(const std::string &json);
std::optional<std::string> parse_extract_json(const std::string &json);
std::string url_encode(const std::string &value);

} // namespace braillatron::net
