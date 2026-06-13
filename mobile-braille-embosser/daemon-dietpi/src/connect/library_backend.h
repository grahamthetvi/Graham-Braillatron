#pragma once

#include "connect_config.h"

#include <string>

namespace braillatron::connect {

class LibraryBackend {
public:
    explicit LibraryBackend(LibraryConfig config);

    std::string search(const std::string &query);
    std::string download(int gutenberg_id);
    std::string list_local() const;
    std::string status() const;

private:
    std::string curl_fetch(const std::string &url) const;
    std::string download_file(const std::string &url, const std::string &dest) const;
    bool register_downloaded_book(int gutenberg_id, const std::string &title,
                                  const std::string &author, const std::string &local_path,
                                  const std::string &format);

    LibraryConfig config_;
};

} // namespace braillatron::connect
