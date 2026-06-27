#pragma once

#include "connect_config.h"

#include <string>

namespace braillatron::connect {

class LibraryBackend {
public:
    explicit LibraryBackend(LibraryConfig config);

    std::string search(const std::string &query, const std::string &source);
    std::string download(const std::string &source, const std::string &result_id);
    std::string list_local() const;
    std::string status() const;

private:
    std::string search_gutendex(const std::string &query);
    std::string search_openlibrary(const std::string &query);
    std::string search_archive(const std::string &query);
    std::string search_librivox(const std::string &query);

    std::string download_gutendex(const std::string &result_id);
    std::string download_openlibrary(const std::string &result_id);
    std::string download_archive(const std::string &result_id);
    std::string download_librivox(const std::string &result_id);

    std::string curl_fetch(const std::string &url, bool archive_api = false) const;
    std::string download_file(const std::string &url, const std::string &dest) const;
    bool register_downloaded_book(const std::string &source, const std::string &external_id,
                                  const std::string &title, const std::string &author,
                                  const std::string &local_path, const std::string &format,
                                  int gutenberg_id = 0);

    LibraryConfig config_;
};

} // namespace braillatron::connect
