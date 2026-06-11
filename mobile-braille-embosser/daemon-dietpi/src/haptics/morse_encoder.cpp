#include "morse_encoder.h"

#include <cctype>
#include <chrono>
#include <thread>
#include <unordered_map>

namespace braillatron::haptics {

namespace {

const std::unordered_map<char, std::string> kMorseTable = {
    {'A', ".-"},   {'B', "-..."}, {'C', "-.-."}, {'D', "-.."},  {'E', "."},
    {'F', "..-."}, {'G', "--."},  {'H', "...."}, {'I', ".."},   {'J', ".---"},
    {'K', "-.-"},  {'L', ".-.."}, {'M', "--"},   {'N', "-."},   {'O', "---"},
    {'P', ".--."}, {'Q', "--.-"}, {'R', ".-."},  {'S', "..."},  {'T', "-"},
    {'U', "..-"},  {'V', "...-"}, {'W', ".--"},  {'X', "-..-"}, {'Y', "-.--"},
    {'Z', "--.."}, {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
    {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."}, {'8', "---.."},
    {'9', "----."},
};

} // namespace

void MorseEncoder::set_pulse_handler(PulseFn fn)
{
    pulse_ = std::move(fn);
}

void MorseEncoder::set_wpm(uint32_t wpm)
{
    wpm_ = wpm > 0 ? wpm : 12;
}

uint32_t MorseEncoder::dot_ms() const
{
    return 1200 / wpm_;
}

std::string MorseEncoder::pattern_for(char ch)
{
    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    const auto it = kMorseTable.find(upper);
    if (it == kMorseTable.end()) {
        return {};
    }
    return it->second;
}

void MorseEncoder::play_character(char ch)
{
    if (!pulse_) {
        return;
    }
    const std::string pattern = pattern_for(ch);
    if (pattern.empty()) {
        return;
    }

    const uint32_t dot = dot_ms();
    const uint32_t dash = dot * 3;
    const uint32_t gap = dot;

    for (char symbol : pattern) {
        if (symbol == '.') {
            pulse_(dot);
        } else if (symbol == '-') {
            pulse_(dash);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(gap));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(dot * 3));
}

void MorseEncoder::play_text(const std::string &text)
{
    for (char ch : text) {
        if (ch == ' ') {
            std::this_thread::sleep_for(std::chrono::milliseconds(dot_ms() * 7));
            continue;
        }
        play_character(ch);
    }
}

} // namespace braillatron::haptics
