#include "edit_session.h"

#include <cctype>

namespace braillatron::documents {

namespace {

constexpr uint8_t kFullCellMask = 0x3F;

} // namespace

void EditSession::set_brf_store(BrfStore *store)
{
    store_ = store;
}

void EditSession::set_announce(AnnounceFn fn)
{
    announce_ = std::move(fn);
}

void EditSession::set_advance_line(AdvanceLineFn fn)
{
    advance_line_ = std::move(fn);
}

const char *EditSession::nav_unit_label(ReviewNavUnit unit)
{
    switch (unit) {
    case ReviewNavUnit::Line:
        return "line";
    case ReviewNavUnit::Word:
        return "word";
    case ReviewNavUnit::Letter:
        return "letter";
    }
    return "line";
}

void EditSession::set_mode(EditMode mode)
{
    mode_ = mode;
    state_ = EditState::EmbossMode;
}

void EditSession::set_nav_unit(ReviewNavUnit unit)
{
    nav_unit_ = unit;
    if (in_review()) {
        snap_column_for_unit();
        state_ = EditState::LineReview;
        if (announce_) {
            announce_(std::string("Review by ") + nav_unit_label(nav_unit_));
        }
        announce_focus();
    } else if (announce_) {
        announce_(std::string("Review by ") + nav_unit_label(nav_unit_));
    }
}

bool EditSession::in_review() const
{
    return state_ == EditState::LineReview || state_ == EditState::AwaitFullCell;
}

std::string EditSession::spoken_char(char ch)
{
    switch (ch) {
    case ' ':
        return "space";
    case '.':
        return "period";
    case ',':
        return "comma";
    case ';':
        return "semicolon";
    case ':':
        return "colon";
    case '!':
        return "exclamation";
    case '?':
        return "question mark";
    case '\'':
        return "apostrophe";
    case '-':
        return "hyphen";
    case '\n':
        return "new line";
    default:
        break;
    }
    const unsigned char uch = static_cast<unsigned char>(ch);
    if (std::isupper(uch)) {
        return std::string("capital ") + static_cast<char>(std::tolower(uch));
    }
    if (std::isprint(uch)) {
        return std::string(1, ch);
    }
    return "character";
}

void EditSession::word_bounds(const std::string &line, size_t col, size_t &start, size_t &end)
{
    if (line.empty()) {
        start = 0;
        end = 0;
        return;
    }
    if (col >= line.size()) {
        col = line.size() - 1;
    }

    if (std::isspace(static_cast<unsigned char>(line[col]))) {
        start = col;
        end = col + 1;
        return;
    }

    start = col;
    while (start > 0 && !std::isspace(static_cast<unsigned char>(line[start - 1]))) {
        --start;
    }
    end = col + 1;
    while (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end]))) {
        ++end;
    }
}

void EditSession::snap_column_for_unit()
{
    if (store_ == nullptr || store_->line_count() == 0) {
        review_column_ = 0;
        return;
    }
    if (review_line_ >= store_->line_count()) {
        review_line_ = store_->line_count() - 1;
    }
    const std::string &line = store_->line_at(review_line_);
    if (line.empty()) {
        review_column_ = 0;
        return;
    }
    if (review_column_ >= line.size()) {
        review_column_ = line.size() - 1;
    }

    if (nav_unit_ == ReviewNavUnit::Word) {
        size_t start = 0;
        size_t end = 0;
        word_bounds(line, review_column_, start, end);
        review_column_ = start;
    }
}

void EditSession::announce_focus() const
{
    if (!announce_ || store_ == nullptr) {
        return;
    }
    if (store_->line_count() == 0) {
        announce_("No lines to review");
        return;
    }

    const std::string &line = store_->line_at(review_line_);
    switch (nav_unit_) {
    case ReviewNavUnit::Line:
        if (line.empty()) {
            announce_("Line " + std::to_string(review_line_ + 1) + ": blank");
        } else {
            announce_("Line " + std::to_string(review_line_ + 1) + ": " + line);
        }
        break;
    case ReviewNavUnit::Word: {
        if (line.empty()) {
            announce_("blank");
            break;
        }
        size_t start = 0;
        size_t end = 0;
        word_bounds(line, review_column_, start, end);
        if (start >= end) {
            announce_("blank");
        } else if (end - start == 1 && std::isspace(static_cast<unsigned char>(line[start]))) {
            announce_("space");
        } else {
            announce_(line.substr(start, end - start));
        }
        break;
    }
    case ReviewNavUnit::Letter:
        if (line.empty()) {
            announce_("blank");
        } else if (review_column_ < line.size()) {
            announce_(spoken_char(line[review_column_]));
        } else {
            announce_("blank");
        }
        break;
    }
}

void EditSession::begin_line_review(size_t line_index)
{
    if (store_ == nullptr) {
        return;
    }
    if (store_->line_count() == 0) {
        state_ = EditState::EmbossMode;
        if (announce_) {
            announce_("No lines to review");
        }
        return;
    }
    if (line_index >= store_->line_count()) {
        line_index = store_->line_count() - 1;
    }
    review_line_ = line_index;
    const std::string &line = store_->line_at(review_line_);
    if (nav_unit_ == ReviewNavUnit::Letter && !line.empty()) {
        review_column_ = line.size() - 1;
    } else if (nav_unit_ == ReviewNavUnit::Word && !line.empty()) {
        size_t start = 0;
        size_t end = 0;
        word_bounds(line, line.size() - 1, start, end);
        review_column_ = start;
    } else {
        review_column_ = 0;
    }
    state_ = EditState::LineReview;
    if (announce_) {
        announce_(std::string("Reviewing by ") + nav_unit_label(nav_unit_));
    }
    announce_focus();
}

bool EditSession::move_by_line(int delta)
{
    const size_t count = store_->line_count();
    if (delta < 0) {
        if (review_line_ == 0) {
            if (announce_) {
                announce_("Start of document");
            }
            return false;
        }
        --review_line_;
    } else if (delta > 0) {
        if (review_line_ + 1 >= count) {
            if (announce_) {
                announce_("End of document");
            }
            return false;
        }
        ++review_line_;
    } else {
        return false;
    }
    review_column_ = 0;
    return true;
}

bool EditSession::move_by_letter(int delta)
{
    const size_t count = store_->line_count();
    if (delta < 0) {
        if (review_column_ > 0) {
            --review_column_;
            return true;
        }
        if (review_line_ == 0) {
            if (announce_) {
                announce_("Start of document");
            }
            return false;
        }
        --review_line_;
        const std::string &prev = store_->line_at(review_line_);
        review_column_ = prev.empty() ? 0 : prev.size() - 1;
        return true;
    }
    if (delta > 0) {
        const std::string &line = store_->line_at(review_line_);
        if (!line.empty() && review_column_ + 1 < line.size()) {
            ++review_column_;
            return true;
        }
        if (review_line_ + 1 >= count) {
            if (announce_) {
                announce_("End of document");
            }
            return false;
        }
        ++review_line_;
        review_column_ = 0;
        return true;
    }
    return false;
}

bool EditSession::move_by_word(int delta)
{
    const size_t count = store_->line_count();
    if (delta < 0) {
        const std::string &line = store_->line_at(review_line_);
        if (!line.empty() && review_column_ > 0) {
            size_t probe = review_column_;
            while (probe > 0 && std::isspace(static_cast<unsigned char>(line[probe - 1]))) {
                --probe;
            }
            if (probe > 0) {
                --probe;
                size_t start = 0;
                size_t end = 0;
                word_bounds(line, probe, start, end);
                review_column_ = start;
                return true;
            }
        }
        if (review_line_ == 0) {
            if (announce_) {
                announce_("Start of document");
            }
            return false;
        }
        --review_line_;
        const std::string &prev = store_->line_at(review_line_);
        if (prev.empty()) {
            review_column_ = 0;
        } else {
            size_t start = 0;
            size_t end = 0;
            word_bounds(prev, prev.size() - 1, start, end);
            review_column_ = start;
        }
        return true;
    }
    if (delta > 0) {
        const std::string &line = store_->line_at(review_line_);
        if (!line.empty()) {
            size_t start = 0;
            size_t end = 0;
            word_bounds(line, review_column_, start, end);
            size_t next = end;
            while (next < line.size() && std::isspace(static_cast<unsigned char>(line[next]))) {
                ++next;
            }
            if (next < line.size()) {
                review_column_ = next;
                return true;
            }
        }
        if (review_line_ + 1 >= count) {
            if (announce_) {
                announce_("End of document");
            }
            return false;
        }
        ++review_line_;
        const std::string &next_line = store_->line_at(review_line_);
        if (next_line.empty()) {
            review_column_ = 0;
        } else {
            size_t col = 0;
            while (col < next_line.size()
                   && std::isspace(static_cast<unsigned char>(next_line[col]))) {
                ++col;
            }
            review_column_ = col < next_line.size() ? col : 0;
        }
        return true;
    }
    return false;
}

bool EditSession::move_review(int delta)
{
    if (!in_review() || store_ == nullptr || store_->line_count() == 0) {
        return false;
    }

    bool moved = false;
    switch (nav_unit_) {
    case ReviewNavUnit::Line:
        moved = move_by_line(delta);
        break;
    case ReviewNavUnit::Word:
        moved = move_by_word(delta);
        break;
    case ReviewNavUnit::Letter:
        moved = move_by_letter(delta);
        break;
    }
    if (!moved) {
        return false;
    }

    state_ = EditState::LineReview;
    announce_focus();
    return true;
}

void EditSession::cancel_review()
{
    if (!in_review() && state_ != EditState::ReplacementLine) {
        return;
    }
    state_ = EditState::EmbossMode;
    if (announce_) {
        announce_("Back to writing");
    }
}

void EditSession::capture_mistake_span()
{
    mistake_word_start_ = 0;
    mistake_word_len_ = 0;
    if (store_ == nullptr || review_line_ >= store_->line_count()) {
        return;
    }
    const std::string &line = store_->line_at(review_line_);
    if (line.empty()) {
        return;
    }

    switch (nav_unit_) {
    case ReviewNavUnit::Line:
        mistake_word_start_ = 0;
        mistake_word_len_ = line.size();
        break;
    case ReviewNavUnit::Word: {
        size_t start = 0;
        size_t end = 0;
        word_bounds(line, review_column_, start, end);
        mistake_word_start_ = start;
        mistake_word_len_ = end > start ? end - start : 0;
        break;
    }
    case ReviewNavUnit::Letter:
        if (review_column_ < line.size()) {
            mistake_word_start_ = review_column_;
            mistake_word_len_ = 1;
        }
        break;
    }
}

void EditSession::on_full_cell(uint8_t dot_mask)
{
    if (state_ != EditState::LineReview && state_ != EditState::AwaitFullCell) {
        return;
    }
    if (dot_mask != kFullCellMask) {
        state_ = EditState::AwaitFullCell;
        return;
    }

    capture_mistake_span();
    state_ = EditState::ReplacementLine;
    if (advance_line_) {
        advance_line_();
    }
    if (announce_) {
        if (mistake_word_len_ == 0) {
            announce_("Replacement ready");
        } else {
            const std::string &line = store_->line_at(review_line_);
            const std::string target = line.substr(mistake_word_start_, mistake_word_len_);
            if (nav_unit_ == ReviewNavUnit::Letter) {
                announce_("Replace " + spoken_char(target[0]));
            } else if (nav_unit_ == ReviewNavUnit::Word) {
                announce_("Replace " + target);
            } else {
                announce_("Replace line");
            }
        }
    }
}

void EditSession::on_replacement_chord(uint8_t dot_mask, const std::string &text)
{
    (void)dot_mask;
    if (state_ != EditState::ReplacementLine || store_ == nullptr || text.empty()) {
        return;
    }

    state_ = EditState::SyncDigital;
    if (mistake_word_len_ == 0 && store_->line_count() > review_line_) {
        // Blank line: insert at column 0.
        mistake_word_start_ = 0;
        store_->insert_word_at(review_line_, 0, text);
    } else {
        store_->delete_word_at(review_line_, mistake_word_start_, mistake_word_len_);
        store_->insert_word_at(review_line_, mistake_word_start_, text);
    }
    store_->save();

    state_ = EditState::EmbossMode;
    if (announce_) {
        announce_("Edit synced: " + text);
    }
}

void EditSession::reset()
{
    state_ = EditState::EmbossMode;
    review_line_ = 0;
    review_column_ = 0;
    mistake_word_start_ = 0;
    mistake_word_len_ = 0;
}

} // namespace braillatron::documents
