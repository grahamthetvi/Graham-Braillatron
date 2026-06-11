#include "edit_session.h"

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

void EditSession::set_mode(EditMode mode)
{
    mode_ = mode;
    state_ = EditState::EmbossMode;
}

void EditSession::begin_line_review(size_t line_index)
{
    if (store_ == nullptr) {
        return;
    }
    review_line_ = line_index;
    state_ = EditState::LineReview;
    if (announce_) {
        announce_("Reviewing line " + std::to_string(line_index + 1) + ": " +
                  store_->line_at(line_index));
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

    state_ = EditState::ReplacementLine;
    if (advance_line_) {
        advance_line_();
    }
    if (announce_) {
        announce_("Replacement line ready");
    }
}

void EditSession::on_replacement_chord(uint8_t dot_mask, const std::string &text)
{
    (void)dot_mask;
    if (state_ != EditState::ReplacementLine || store_ == nullptr || text.empty()) {
        return;
    }

    state_ = EditState::SyncDigital;
    const std::string &line = store_->line_at(review_line_);
    mistake_word_start_ = 0;
    mistake_word_len_ = line.size();

    store_->delete_word_at(review_line_, mistake_word_start_, mistake_word_len_);
    store_->insert_word_at(review_line_, mistake_word_start_, text);
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
}

} // namespace braillatron::documents
