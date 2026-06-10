#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace braillatron::keyboard {

class EvdevKeymap {
public:
    static EvdevKeymap load(const std::string &path);

    uint16_t logical_mask_for_code(unsigned evdev_code) const;
    bool has_mapping(unsigned evdev_code) const;

private:
    std::unordered_map<unsigned, uint16_t> code_to_mask_;
};

unsigned evdev_code_from_name(const std::string &name);

} // namespace braillatron::keyboard
