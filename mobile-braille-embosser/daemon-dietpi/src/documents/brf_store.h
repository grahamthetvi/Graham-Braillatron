#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace braillatron::documents {

class BrfStore {
public:
    explicit BrfStore(std::string path);

    const std::string &path() const { return path_; }
    const std::vector<std::string> &lines() const { return lines_; }

    bool load();
    bool save() const;
    void set_lines(std::vector<std::string> lines);

    void append_line(const std::string &line);
    void append_char(char ch);
    void backspace();

    std::string line_at(size_t index) const;
    size_t line_count() const { return lines_.size(); }

    bool delete_word_at(size_t line_index, size_t word_start, size_t word_len);
    bool insert_word_at(size_t line_index, size_t column, const std::string &word);

    std::string full_text() const;

private:
    std::string path_;
    std::vector<std::string> lines_;
};

} // namespace braillatron::documents
