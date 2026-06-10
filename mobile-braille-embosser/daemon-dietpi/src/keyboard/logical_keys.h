#pragma once

#include <cstdint>
#include <string>

namespace braillatron::keyboard {

uint16_t logical_mask_from_name(const std::string &name);

} // namespace braillatron::keyboard
