#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace braillatron::documents {

enum class BrailleGradePreset {
    UebG1Math,
    UebG1Nemeth,
    UebG2Math,
    UebG2Nemeth,
};

const char *braille_grade_preset_config_value(BrailleGradePreset preset);
const char *braille_grade_preset_display_label(BrailleGradePreset preset);
BrailleGradePreset braille_grade_preset_from_string(const std::string &name);
BrailleGradePreset next_braille_grade_preset(BrailleGradePreset preset);

class BrailleTranslationService {
public:
    explicit BrailleTranslationService(BrailleGradePreset preset = BrailleGradePreset::UebG2Math);

    bool available() const
    {
        const_cast<BrailleTranslationService *>(this)->ensure_tables_ready();
        return available_;
    }

    BrailleGradePreset current_preset() const { return preset_; }
    const char *display_label() const { return braille_grade_preset_display_label(preset_); }
    const char *config_value() const { return braille_grade_preset_config_value(preset_); }

    void set_grade_preset(BrailleGradePreset preset);
    void set_grade_preset_from_config(const std::string &name);

    std::string translate_forward(const std::string &plain) const;
    std::optional<std::string> translate_backward_dots(uint8_t dot_mask) const;

private:
    const char *table_list() const;
    void refresh_table_list();
    bool verify_available() const;
    void ensure_tables_ready();

    BrailleGradePreset preset_ = BrailleGradePreset::UebG2Math;
    std::string resolved_table_list_;
    bool available_ = false;
    bool tables_verified_ = false;
};

uint8_t braille_char_to_dot_mask(wchar_t braille_char);

} // namespace braillatron::documents
