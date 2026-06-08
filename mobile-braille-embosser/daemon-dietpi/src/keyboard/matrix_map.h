#pragma once

#include <cstdint>
#include <string>

namespace braillatron::keyboard {

class MatrixMap {
public:
    MatrixMap();

    static MatrixMap load(const std::string &path);

    uint16_t remap(uint16_t physical_state) const;

private:
    uint16_t logical_mask_for_physical_[16];
};

} // namespace braillatron::keyboard
