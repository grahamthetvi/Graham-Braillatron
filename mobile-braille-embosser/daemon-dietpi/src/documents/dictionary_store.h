#pragma once

#include <string>
#include <vector>

namespace braillatron::documents {

struct DictionaryEntry {
    std::string word;
    std::string part_of_speech;
    std::string definition;
};

struct DictionaryConfig {
    std::string db_path = "/data/braillatron/dictionary/en.sqlite";
    int max_definitions = 5;
    bool emboss_enabled = false;
};

DictionaryConfig load_dictionary_config(const std::string &path);

class DictionaryStore {
public:
    explicit DictionaryStore(DictionaryConfig config = {});

    bool open();
    void close();
    bool is_open() const { return db_ != nullptr; }

    std::vector<DictionaryEntry> lookup(const std::string &word) const;
    std::vector<std::string> prefix_matches(const std::string &prefix, size_t limit = 20) const;

private:
    DictionaryConfig config_;
    void *db_ = nullptr;
};

} // namespace braillatron::documents
