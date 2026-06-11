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

    void set_mode(EditMode mode);
    void begin_line_review(size_t line_index);
    void on_full_cell(uint8_t dot_mask);
    void on_replacement_chord(uint8_t dot_mask, const std::string &text);
    void reset();

private:
    BrfStore *store_ = nullptr;
    AnnounceFn announce_;
    AdvanceLineFn advance_line_;
    EditMode mode_ = EditMode::Emboss;
    EditState state_ = EditState::EmbossMode;
    size_t review_line_ = 0;
    size_t mistake_word_start_ = 0;
    size_t mistake_word_len_ = 0;
};

} // namespace braillatron::documents
