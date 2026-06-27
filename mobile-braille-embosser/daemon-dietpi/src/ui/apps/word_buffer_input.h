#pragma once

#include "../../documents/liblouis_bridge.h"

#include <cstdint>
#include <string>
#include <vector>

namespace braillatron::ui {

class WordBufferInput {
public:
    void clear();
    bool empty() const;
    bool has_pending() const;
    void push_chord(uint8_t dot_mask, const documents::BrailleTranslationService *braille_input);
    bool pop_chord(const documents::BrailleTranslationService *braille_input);
    const std::string &preview() const;
    std::string commit_word(const documents::BrailleTranslationService *braille_input);

private:
    void rebuild_preview(const documents::BrailleTranslationService *braille_input);

    std::vector<uint8_t> pending_chords_;
    std::string pending_preview_;
};

} // namespace braillatron::ui
