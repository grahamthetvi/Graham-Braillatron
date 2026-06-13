#pragma once

#include <string>
#include <vector>

namespace braillatron::documents {

struct SpellingList {
    std::string id;
    std::string name;
    std::vector<std::string> words;
};

struct SpellingConfig {
    std::string default_list_id = "grade3_week1";
    std::string bundled_dir = "/usr/share/braillatron/spelling";
    std::string custom_dir = "/data/braillatron/spelling-lists";
    std::string session_dir = "/data/braillatron/spelling-sessions";
    std::string braille_table = "ueb_g2";
    bool sentence_tts = true;
};

struct SpellingSessionState {
    std::string list_id;
    std::vector<std::string> missed_words;
    int score = 0;
    int attempts = 0;
};

SpellingConfig load_spelling_config(const std::string &path);

class SpellingListStore {
public:
    explicit SpellingListStore(SpellingConfig config = {});

    void refresh();
    const std::vector<SpellingList> &lists() const { return lists_; }
    const SpellingList *find_list(const std::string &id) const;
    bool import_file(const std::string &path, SpellingList *out = nullptr);
    bool save_session(const SpellingSessionState &session, const std::string &timestamp) const;

private:
    bool load_json_file(const std::string &path, SpellingList *out);
    bool load_csv_file(const std::string &path, SpellingList *out);

    SpellingConfig config_;
    std::vector<SpellingList> lists_;
};

} // namespace braillatron::documents
