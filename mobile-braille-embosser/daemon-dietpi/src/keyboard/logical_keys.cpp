#include "logical_keys.h"

extern "C" {
#include "protocol.h"
}

#include <cctype>
#include <stdexcept>

namespace braillatron::keyboard {

namespace {

std::string trim(const std::string &value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return value.substr(start, end - start);
}

std::string to_lower(std::string value)
{
    for (char &ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

} // namespace

uint16_t logical_mask_from_name(const std::string &name)
{
    const std::string key = to_lower(trim(name));

    if (key.empty() || key == "unused" || key == "none" || key == "reserved") {
        return 0u;
    }
    if (key == "dot_1") {
        return BRAILLATRON_KEY_DOT_1;
    }
    if (key == "dot_2") {
        return BRAILLATRON_KEY_DOT_2;
    }
    if (key == "dot_3") {
        return BRAILLATRON_KEY_DOT_3;
    }
    if (key == "dot_4") {
        return BRAILLATRON_KEY_DOT_4;
    }
    if (key == "dot_5") {
        return BRAILLATRON_KEY_DOT_5;
    }
    if (key == "dot_6") {
        return BRAILLATRON_KEY_DOT_6;
    }
    if (key == "dpad_up") {
        return BRAILLATRON_KEY_DPAD_UP;
    }
    if (key == "dpad_down") {
        return BRAILLATRON_KEY_DPAD_DOWN;
    }
    if (key == "backspace") {
        return BRAILLATRON_KEY_BACKSPACE;
    }
    if (key == "enter") {
        return BRAILLATRON_KEY_ENTER;
    }
    if (key == "shift_tts") {
        return BRAILLATRON_KEY_SHIFT_TTS;
    }
    if (key == "speech") {
        return BRAILLATRON_KEY_SPEECH;
    }
    if (key == "menu") {
        return BRAILLATRON_KEY_MENU;
    }

    throw std::runtime_error("unknown logical key name: " + name);
}

} // namespace braillatron::keyboard
