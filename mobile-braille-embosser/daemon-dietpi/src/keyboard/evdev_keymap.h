#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace braillatron::keyboard {

class EvdevKeymap {
public:
    static EvdevKeymap load(const std::string &path);

    uint16_t logical_mask_for_code(unsigned evdev_code) const;
    std::optional<char> text_for_code(unsigned evdev_code) const;
    bool has_mapping(unsigned evdev_code) const;

private:
    std::unordered_map<unsigned, uint16_t> code_to_mask_;
    std::unordered_map<unsigned, char> code_to_text_;
};

unsigned evdev_code_from_name(const std::string &name);

} // namespace braillatron::keyboard
