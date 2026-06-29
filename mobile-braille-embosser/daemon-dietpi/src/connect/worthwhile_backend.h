#pragma once

#include "connect_config.h"

#include <string>
#include <vector>

namespace braillatron::connect {

class WorthwhileBackend {
public:
    explicit WorthwhileBackend(WorthwhileConfig config);

    std::string search(const std::string &query, const std::string &kind);
    std::string recent(const std::string &kind);
    std::string download(const std::string &item_id);
    std::string status() const;

private:
    struct CatalogItem {
        std::string id;
        std::string title;
        std::string path_segment;
    };

    std::string origin() const;
    std::string segment_movies() const;
    std::string segment_shows() const;
    std::string segment_login() const;
    std::string segment_fetch() const;

    std::string browser_agent() const;
    std::string curl_fetch(const std::string &url, bool follow_redirects = true) const;
    int curl_download(const std::string &url, const std::string &dest) const;
    bool ensure_session();
    bool load_credentials(std::string &email, std::string &password) const;
    std::string read_xsrf_token() const;
    std::string extract_hidden_token(const std::string &html) const;
    std::vector<CatalogItem> parse_catalog_table(const std::string &html) const;
    std::vector<CatalogItem> parse_recent_block(const std::string &html,
                                                const std::string &kind) const;
    std::string catalog_json(const std::vector<CatalogItem> &items,
                             const std::string &media_kind) const;
    std::string filename_from_headers(const std::string &headers) const;
    std::string sanitize_filename(const std::string &value) const;
    std::string shell_quote(const std::string &value) const;

    WorthwhileConfig config_;
};

} // namespace braillatron::connect
