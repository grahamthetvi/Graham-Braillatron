#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace braillatron::haptics {

class MorseEncoder {
public:
    using PulseFn = std::function<void(uint32_t duration_ms)>;

    void set_pulse_handler(PulseFn fn);
    void set_wpm(uint32_t wpm);

    void play_text(const std::string &text);
    void play_character(char ch);
    static std::string pattern_for(char ch);

private:
    PulseFn pulse_;
    uint32_t wpm_ = 12;
    uint32_t dot_ms() const;
};

} // namespace braillatron::haptics
