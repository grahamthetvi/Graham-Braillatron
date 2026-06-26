#include "liblouis_bridge.h"

#include <cstdlib>
#include <iostream>
#include <vector>

#if defined(BRAILLATRON_LIBLOUIS) || defined(BRAILLATRON_A11Y)
#include <liblouis/liblouis.h>
#define BRAILLATRON_HAS_LIBLOUIS 1
#endif

namespace braillatron::documents {

namespace {

#ifdef BRAILLATRON_HAS_LIBLOUIS
bool g_liblouis_initialized = false;

void ensure_liblouis_initialized()
{
    if (g_liblouis_initialized) {
        return;
    }

    const char *table_path = std::getenv("LOUIS_TABLEPATH");
    if (table_path == nullptr || table_path[0] == '\0') {
        table_path = "/usr/share/liblouis/tables";
    }
    lou_setDataPath(table_path);
    g_liblouis_initialized = true;
}
#endif

const char *nemeth_overlay_table()
{
    const char *override_table = std::getenv("LOUIS_NEMETH_TABLE");
    if (override_table != nullptr && override_table[0] != '\0') {
        return override_table;
    }
    return "en-us-mathtext.ctb";
}

std::string preset_table_list_string(BrailleGradePreset preset)
{
    switch (preset) {
    case BrailleGradePreset::UebG1Math:
        return "en-ueb-g1.ctb";
    case BrailleGradePreset::UebG1Nemeth:
        return std::string("en-ueb-g1.ctb,") + nemeth_overlay_table();
    case BrailleGradePreset::UebG2Math:
        return "en-ueb-g2.ctb";
    case BrailleGradePreset::UebG2Nemeth:
        return std::string("en-ueb-g2.ctb,") + nemeth_overlay_table();
    }
    return "en-ueb-g2.ctb";
}

#ifdef BRAILLATRON_HAS_LIBLOUIS
bool table_list_back_translate_smoke_test(const std::string &table_list)
{
    widechar inbuf[2] = {static_cast<widechar>(0x2801), 0};
    int inlen = 1;
    widechar outbuf[16] = {0};
    int outlen = 15;
    if (lou_backTranslateString(table_list.c_str(), inbuf, &inlen, outbuf, &outlen, nullptr, nullptr,
                                0) == 0) {
        return false;
    }
    return outlen > 0 && outbuf[0] == L'a';
}

std::string resolve_table_list(BrailleGradePreset preset)
{
    ensure_liblouis_initialized();

    const std::string primary = preset_table_list_string(preset);
    if (table_list_back_translate_smoke_test(primary)) {
        return primary;
    }

    if (preset == BrailleGradePreset::UebG1Nemeth ||
        preset == BrailleGradePreset::UebG2Nemeth) {
        const std::string literary_fallback = preset == BrailleGradePreset::UebG1Nemeth
                                                  ? "en-ueb-g1.ctb"
                                                  : "en-ueb-g2.ctb";
        std::cerr << "[liblouis] preset " << braille_grade_preset_config_value(preset)
                  << ": Nemeth overlay unavailable, using literary table "
                  << literary_fallback << "\n";
        return literary_fallback;
    }

    return primary;
}
#else
std::string resolve_table_list(BrailleGradePreset preset)
{
    return preset_table_list_string(preset);
}
#endif

} // namespace

const char *braille_grade_preset_config_value(BrailleGradePreset preset)
{
    switch (preset) {
    case BrailleGradePreset::UebG1Math:
        return "ueb_g1_math";
    case BrailleGradePreset::UebG1Nemeth:
        return "ueb_g1_nemeth";
    case BrailleGradePreset::UebG2Math:
        return "ueb_g2_math";
    case BrailleGradePreset::UebG2Nemeth:
        return "ueb_g2_nemeth";
    }
    return "ueb_g2_math";
}

const char *braille_grade_preset_display_label(BrailleGradePreset preset)
{
    switch (preset) {
    case BrailleGradePreset::UebG1Math:
        return "UEB Grade 1, UEB math";
    case BrailleGradePreset::UebG1Nemeth:
        return "UEB Grade 1, Nemeth math";
    case BrailleGradePreset::UebG2Math:
        return "UEB Grade 2, UEB math";
    case BrailleGradePreset::UebG2Nemeth:
        return "UEB Grade 2, Nemeth math";
    }
    return "UEB Grade 2, UEB math";
}

BrailleGradePreset braille_grade_preset_from_string(const std::string &name)
{
    if (name == "ueb_g1_math") {
        return BrailleGradePreset::UebG1Math;
    }
    if (name == "ueb_g1_nemeth") {
        return BrailleGradePreset::UebG1Nemeth;
    }
    if (name == "ueb_g2_math") {
        return BrailleGradePreset::UebG2Math;
    }
    if (name == "ueb_g2_nemeth") {
        return BrailleGradePreset::UebG2Nemeth;
    }

    // Legacy aliases.
    if (name == "ueb_g1" || name == "g1") {
        return BrailleGradePreset::UebG1Math;
    }
    if (name == "ueb_g2" || name == "g2") {
        return BrailleGradePreset::UebG2Math;
    }
    if (name == "nemeth") {
        return BrailleGradePreset::UebG2Nemeth;
    }
    return BrailleGradePreset::UebG2Math;
}

BrailleGradePreset next_braille_grade_preset(BrailleGradePreset preset)
{
    switch (preset) {
    case BrailleGradePreset::UebG1Math:
        return BrailleGradePreset::UebG1Nemeth;
    case BrailleGradePreset::UebG1Nemeth:
        return BrailleGradePreset::UebG2Math;
    case BrailleGradePreset::UebG2Math:
        return BrailleGradePreset::UebG2Nemeth;
    case BrailleGradePreset::UebG2Nemeth:
        return BrailleGradePreset::UebG1Math;
    }
    return BrailleGradePreset::UebG1Math;
}

const char *braille_input_preset_config_value(BrailleInputPreset preset)
{
    switch (preset) {
    case BrailleInputPreset::UebMath:
        return "ueb_g2_math";
    case BrailleInputPreset::Nemeth:
        return "nemeth";
    }
    return "ueb_g2_math";
}

const char *braille_input_preset_display_label(BrailleInputPreset preset)
{
    switch (preset) {
    case BrailleInputPreset::UebMath:
        return "UEB Math";
    case BrailleInputPreset::Nemeth:
        return "Nemeth";
    }
    return "UEB Math";
}

BrailleInputPreset braille_input_preset_from_string(const std::string &name)
{
    if (name == "nemeth" || name == "ueb_g2_nemeth" || name == "ueb_g1_nemeth") {
        return BrailleInputPreset::Nemeth;
    }
    return BrailleInputPreset::UebMath;
}

BrailleInputPreset next_braille_input_preset(BrailleInputPreset preset)
{
    switch (preset) {
    case BrailleInputPreset::UebMath:
        return BrailleInputPreset::Nemeth;
    case BrailleInputPreset::Nemeth:
        return BrailleInputPreset::UebMath;
    }
    return BrailleInputPreset::UebMath;
}

BrailleGradePreset braille_grade_for_input_preset(BrailleInputPreset preset)
{
    switch (preset) {
    case BrailleInputPreset::Nemeth:
        return BrailleGradePreset::UebG2Nemeth;
    case BrailleInputPreset::UebMath:
        return BrailleGradePreset::UebG2Math;
    }
    return BrailleGradePreset::UebG2Math;
}

bool braille_input_uses_nemeth_overlay(BrailleInputPreset preset)
{
    return preset == BrailleInputPreset::Nemeth;
}

BrailleTranslationService::BrailleTranslationService(BrailleGradePreset preset)
    : preset_(preset)
    , resolved_table_list_(preset_table_list_string(preset))
{
}

void BrailleTranslationService::ensure_tables_ready()
{
    if (tables_verified_) {
        return;
    }

    refresh_table_list();
    available_ = verify_available();
    tables_verified_ = true;

#ifndef BRAILLATRON_HAS_LIBLOUIS
    std::cerr << "[liblouis] translation unavailable (rebuild with BRAILLATRON_LIBLOUIS=1)\n";
#else
    if (!available_) {
        std::cerr << "[liblouis] translation unavailable (check liblouis-data / LOUIS_TABLEPATH)\n";
    }
#endif
}

void BrailleTranslationService::set_grade_preset(BrailleGradePreset preset)
{
    preset_ = preset;
    tables_verified_ = false;
    resolved_table_list_ = preset_table_list_string(preset_);
    ensure_tables_ready();
}

void BrailleTranslationService::refresh_table_list()
{
    const std::string expected = preset_table_list_string(preset_);
    resolved_table_list_ = resolve_table_list(preset_);
    nemeth_overlay_active_ =
        (preset_ == BrailleGradePreset::UebG1Nemeth ||
         preset_ == BrailleGradePreset::UebG2Nemeth) &&
        resolved_table_list_ == expected;
}

void BrailleTranslationService::set_grade_preset_from_config(const std::string &name)
{
    set_grade_preset(braille_grade_preset_from_string(name));
}

const char *BrailleTranslationService::table_list() const
{
    return resolved_table_list_.c_str();
}

bool BrailleTranslationService::verify_available() const
{
#ifdef BRAILLATRON_HAS_LIBLOUIS
    ensure_liblouis_initialized();
    widechar inbuf[2] = {static_cast<widechar>(0x2801), 0};
    int inlen = 1;
    widechar outbuf[16] = {0};
    int outlen = 15;
    return lou_backTranslateString(table_list(), inbuf, &inlen, outbuf, &outlen, nullptr, nullptr,
                                   0) != 0;
#else
    (void)preset_;
    return false;
#endif
}

std::string BrailleTranslationService::translate_forward(const std::string &plain) const
{
    if (plain.empty()) {
        return {};
    }

    const_cast<BrailleTranslationService *>(this)->ensure_tables_ready();

#ifdef BRAILLATRON_HAS_LIBLOUIS
    if (!available_) {
        return plain;
    }

    ensure_liblouis_initialized();

    std::vector<widechar> wide;
    wide.reserve(plain.size() + 1);
    for (unsigned char ch : plain) {
        wide.push_back(static_cast<widechar>(ch));
    }
    wide.push_back(0);

    widechar outbuf[512] = {0};
    int inlen = static_cast<int>(wide.size() - 1);
    int outlen = static_cast<int>(sizeof(outbuf) / sizeof(outbuf[0]) - 1);

    if (lou_translateString(table_list(), wide.data(), &inlen, outbuf, &outlen, nullptr, nullptr,
                          0)) {
        std::string result;
        for (int i = 0; i < outlen; ++i) {
            if (outbuf[i] < 128) {
                result.push_back(static_cast<char>(outbuf[i]));
            }
        }
        return result;
    }
#endif
    return plain;
}

std::optional<std::string> BrailleTranslationService::translate_backward_dots(
    uint8_t dot_mask) const
{
    if (dot_mask == 0) {
        return std::string(" ");
    }

    const_cast<BrailleTranslationService *>(this)->ensure_tables_ready();

#ifdef BRAILLATRON_HAS_LIBLOUIS
    if (!available_) {
        return std::nullopt;
    }

    ensure_liblouis_initialized();

    widechar inbuf[2] = {static_cast<widechar>(0x2800 | dot_mask), 0};
    int inlen = 1;
    widechar outbuf[16] = {0};
    int outlen = 15;

    if (lou_backTranslateString(table_list(), inbuf, &inlen, outbuf, &outlen, nullptr, nullptr,
                                0)) {
        std::string result;
        for (int i = 0; i < outlen; ++i) {
            result.push_back(static_cast<char>(outbuf[i]));
        }
        return result;
    }
    return std::nullopt;
#else
    (void)dot_mask;
    return std::nullopt;
#endif
}

uint8_t braille_char_to_dot_mask(wchar_t braille_char)
{
    if (braille_char < 0x2800 || braille_char > 0x28FF) {
        return 0;
    }
    return static_cast<uint8_t>(braille_char & 0x3F);
}

} // namespace braillatron::documents
