#include "liblouis_bridge.h"

#include <iostream>
#include <string>

namespace {

bool expect_back_translation(braillatron::documents::BrailleTranslationService &service,
                             braillatron::documents::BrailleGradePreset preset,
                             uint8_t dot_mask, const std::string &expected)
{
    service.set_grade_preset(preset);
    const auto result = service.translate_backward_dots(dot_mask);
    if (!result.has_value()) {
        std::cerr << "liblouis self-test: preset "
                  << braillatron::documents::braille_grade_preset_config_value(preset)
                  << " dot 0x" << std::hex << static_cast<unsigned>(dot_mask) << std::dec
                  << " back-translate failed\n";
        return false;
    }
    if (*result != expected) {
        std::cerr << "liblouis self-test: preset "
                  << braillatron::documents::braille_grade_preset_config_value(preset)
                  << " dot 0x" << std::hex << static_cast<unsigned>(dot_mask) << std::dec
                  << " expected '" << expected << "' got '" << *result << "'\n";
        return false;
    }
    return true;
}

bool expect_round_trip(braillatron::documents::BrailleTranslationService &service,
                       braillatron::documents::BrailleGradePreset preset,
                       const std::string &plain)
{
    service.set_grade_preset(preset);
    const std::string forward = service.translate_forward(plain);
    if (forward.empty()) {
        std::cerr << "liblouis self-test: forward translation empty for '" << plain << "'\n";
        return false;
    }

    uint8_t combined = 0;
    for (unsigned char ch : forward) {
        const uint8_t mask = braillatron::documents::braille_char_to_dot_mask(ch);
        if (mask == 0) {
            continue;
        }
        combined = mask;
        break;
    }

    const auto back = service.translate_backward_dots(combined);
    if (!back.has_value() || back->empty()) {
        std::cerr << "liblouis self-test: round-trip back-translate failed for '" << plain
                  << "'\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    using namespace braillatron::documents;

    BrailleTranslationService service(BrailleGradePreset::UebG2Math);
    if (!service.available()) {
        std::cerr << "liblouis self-test skipped: liblouis unavailable "
                     "(install liblouis-data and rebuild with BRAILLATRON_LIBLOUIS=1)\n";
        return 0;
    }

    const BrailleGradePreset presets[] = {
        BrailleGradePreset::UebG1Math,
        BrailleGradePreset::UebG1Nemeth,
        BrailleGradePreset::UebG2Math,
        BrailleGradePreset::UebG2Nemeth,
    };

    for (BrailleGradePreset preset : presets) {
        if (!expect_back_translation(service, preset, 0x01, "a")) {
            return 1;
        }
        if (!expect_round_trip(service, preset, "a")) {
            return 1;
        }
    }

    service.set_grade_preset(braille_grade_for_input_preset(BrailleInputPreset::UebMath));
    if (!expect_back_translation(service, BrailleGradePreset::UebG2Math, 0x01, "a")) {
        return 1;
    }
    service.set_grade_preset(braille_grade_for_input_preset(BrailleInputPreset::Nemeth));
    if (!expect_back_translation(service, BrailleGradePreset::UebG2Nemeth, 0x01, "a")) {
        return 1;
    }

    std::cout << "liblouis self-test ok\n";
    return 0;
}
