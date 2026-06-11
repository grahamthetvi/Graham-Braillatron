#include "brf_store.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace braillatron::documents {

namespace fs = std::filesystem;

BrfStore::BrfStore(std::string path)
    : path_(std::move(path))
{
}

bool BrfStore::load()
{
    lines_.clear();
    std::ifstream input(path_);
    if (!input.is_open()) {
        lines_.push_back("");
        return false;
    }

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines_.push_back(line);
    }
    if (lines_.empty()) {
        lines_.push_back("");
    }
    return true;
}

bool BrfStore::save() const
{
    const fs::path path(path_);
    if (path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
    }

    const std::string temp_path = path_ + ".tmp";
    {
        std::ofstream output(temp_path, std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }
        for (size_t i = 0; i < lines_.size(); ++i) {
            output << lines_[i];
            if (i + 1 < lines_.size()) {
                output << '\n';
            }
        }
        output.flush();
        if (!output.good()) {
            return false;
        }
    }

    std::error_code ec;
    fs::rename(temp_path, path_, ec);
    return !ec;
}

void BrfStore::set_lines(std::vector<std::string> lines)
{
    lines_ = std::move(lines);
    if (lines_.empty()) {
        lines_.push_back("");
    }
}

void BrfStore::append_line(const std::string &line)
{
    if (lines_.empty()) {
        lines_.push_back(line);
        return;
    }
    lines_.back() += line;
}

void BrfStore::append_char(char ch)
{
    if (lines_.empty()) {
        lines_.push_back("");
    }
    if (ch == '\n') {
        lines_.push_back("");
        return;
    }
    lines_.back().push_back(ch);
}

void BrfStore::backspace()
{
    if (lines_.empty()) {
        return;
    }
    if (!lines_.back().empty()) {
        lines_.back().pop_back();
        return;
    }
    if (lines_.size() > 1) {
        lines_.pop_back();
    }
}

std::string BrfStore::line_at(size_t index) const
{
    if (index >= lines_.size()) {
        return {};
    }
    return lines_[index];
}

bool BrfStore::delete_word_at(size_t line_index, size_t word_start, size_t word_len)
{
    if (line_index >= lines_.size() || word_start + word_len > lines_[line_index].size()) {
        return false;
    }
    std::string &line = lines_[line_index];
    line.erase(word_start, word_len);
    return true;
}

bool BrfStore::insert_word_at(size_t line_index, size_t column, const std::string &word)
{
    if (line_index >= lines_.size() || column > lines_[line_index].size()) {
        return false;
    }
    lines_[line_index].insert(column, word);
    return true;
}

std::string BrfStore::full_text() const
{
    std::ostringstream stream;
    for (size_t i = 0; i < lines_.size(); ++i) {
        stream << lines_[i];
        if (i + 1 < lines_.size()) {
            stream << '\n';
        }
    }
    return stream.str();
}

} // namespace braillatron::documents
