#include "audio_output.h"

#include <iostream>
#include <string>

namespace {

bool expect_mac(const std::string &input, const std::string &expected)
{
    const auto normalized = braillatron::platform::normalize_mac(input);
    if (!normalized.has_value()) {
        std::cerr << "normalize_mac: expected value for '" << input << "'\n";
        return false;
    }
    if (*normalized != expected) {
        std::cerr << "normalize_mac: '" << input << "' -> '" << *normalized << "', expected '"
                  << expected << "'\n";
        return false;
    }
    return true;
}

bool test_normalize_mac()
{
    if (!expect_mac("aa:bb:cc:dd:ee:ff", "AA:BB:CC:DD:EE:FF")) {
        return false;
    }
    if (!expect_mac("AABBCCDDEEFF", "AA:BB:CC:DD:EE:FF")) {
        return false;
    }
    if (!expect_mac("aa-bb-cc-dd-ee-ff", "AA:BB:CC:DD:EE:FF")) {
        return false;
    }
    if (braillatron::platform::normalize_mac("not-a-mac").has_value()) {
        std::cerr << "normalize_mac: rejected invalid input\n";
        return false;
    }
    if (braillatron::platform::normalize_mac("aa:bb:cc").has_value()) {
        std::cerr << "normalize_mac: rejected short input\n";
        return false;
    }
    return true;
}

bool test_mode_display_label()
{
    if (braillatron::platform::mode_display_label("aux") != "Aux jack") {
        std::cerr << "mode_display_label: aux mismatch\n";
        return false;
    }
    if (braillatron::platform::mode_display_label("bluetooth") != "Bluetooth") {
        std::cerr << "mode_display_label: bluetooth mismatch\n";
        return false;
    }
    if (braillatron::platform::mode_display_label("i2s") != "I2S amplifier") {
        std::cerr << "mode_display_label: i2s mismatch\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!test_normalize_mac() || !test_mode_display_label()) {
        return 1;
    }

    std::cout << "audio_output self-test ok\n";
    return 0;
}
