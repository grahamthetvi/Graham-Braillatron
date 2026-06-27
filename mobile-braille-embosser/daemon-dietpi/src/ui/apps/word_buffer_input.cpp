#include "word_buffer_input.h"

namespace braillatron::ui {

void WordBufferInput::clear()
{
    pending_chords_.clear();
    pending_preview_.clear();
}

bool WordBufferInput::empty() const
{
    return pending_chords_.empty();
}

bool WordBufferInput::has_pending() const
{
    return !pending_chords_.empty();
}

void WordBufferInput::push_chord(uint8_t dot_mask,
                               const documents::BrailleTranslationService *braille_input)
{
    if (dot_mask == 0) {
        return;
    }
    pending_chords_.push_back(dot_mask);
    rebuild_preview(braille_input);
}

bool WordBufferInput::pop_chord(const documents::BrailleTranslationService *braille_input)
{
    if (pending_chords_.empty()) {
        return false;
    }
    pending_chords_.pop_back();
    rebuild_preview(braille_input);
    return true;
}

const std::string &WordBufferInput::preview() const
{
    return pending_preview_;
}

void WordBufferInput::rebuild_preview(const documents::BrailleTranslationService *braille_input)
{
    pending_preview_.clear();
    if (braille_input == nullptr) {
        return;
    }

    for (uint8_t dot_mask : pending_chords_) {
        const auto cell = braille_input->translate_backward_dot_uncontracted(dot_mask);
        if (!cell.has_value() || cell->empty()) {
            pending_preview_.push_back('?');
            continue;
        }
        pending_preview_ += *cell;
    }
}

std::string WordBufferInput::commit_word(const documents::BrailleTranslationService *braille_input)
{
    if (pending_chords_.empty()) {
        return {};
    }

    std::string word;
    if (braille_input != nullptr) {
        if (const auto translated = braille_input->translate_backward_cells(pending_chords_)) {
            word = *translated;
        }
    }
    if (word.empty()) {
        rebuild_preview(braille_input);
        word = pending_preview_;
    }

    clear();
    return word;
}

} // namespace braillatron::ui
