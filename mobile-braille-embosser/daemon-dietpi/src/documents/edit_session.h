#pragma once

#include "brf_store.h"

#include <cstdint>
#include <functional>
#include <string>

namespace braillatron::documents {

enum class EditMode {
    Emboss,
    EditViaAudio,
    EmbossAndEdit,
};

enum class EditState {
    EmbossMode,
    LineReview,
    AwaitFullCell,
    ReplacementLine,
    SyncDigital,
};

/** Granularity for D-pad review navigation (selected from Document menu). */
enum class ReviewNavUnit {
    Line,
    Word,
    Letter,
};

class EditSession {
public:
    using AnnounceFn = std::function<void(const std::string &)>;
    using AdvanceLineFn = std::function<void()>;

    void set_brf_store(BrfStore *store);
    void set_announce(AnnounceFn fn);
    void set_advance_line(AdvanceLineFn fn);

    EditMode mode() const { return mode_; }
    EditState state() const { return state_; }
    size_t review_line() const { return review_line_; }
    size_t review_column() const { return review_column_; }
    ReviewNavUnit nav_unit() const { return nav_unit_; }
    static const char *nav_unit_label(ReviewNavUnit unit);

    void set_mode(EditMode mode);
    void set_nav_unit(ReviewNavUnit unit);
    void begin_line_review(size_t line_index);
    /** Move review focus by one nav unit. Returns true if position changed. */
    bool move_review(int delta);
    void cancel_review();
    void on_full_cell(uint8_t dot_mask);
    void on_replacement_chord(uint8_t dot_mask, const std::string &text);
    void reset();

private:
    bool in_review() const;
    void announce_focus() const;
    void snap_column_for_unit();
    void capture_mistake_span();
    bool move_by_line(int delta);
    bool move_by_word(int delta);
    bool move_by_letter(int delta);
    static void word_bounds(const std::string &line, size_t col, size_t &start, size_t &end);
    static std::string spoken_char(char ch);

    BrfStore *store_ = nullptr;
    AnnounceFn announce_;
    AdvanceLineFn advance_line_;
    EditMode mode_ = EditMode::Emboss;
    EditState state_ = EditState::EmbossMode;
    ReviewNavUnit nav_unit_ = ReviewNavUnit::Line;
    size_t review_line_ = 0;
    size_t review_column_ = 0;
    size_t mistake_word_start_ = 0;
    size_t mistake_word_len_ = 0;
};

} // namespace braillatron::documents
