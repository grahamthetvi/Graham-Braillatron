#pragma once

#include <cstdint>

namespace braillatron::kinematics {

class PaperPosition {
public:
    int32_t y_line_index() const { return y_line_index_; }

    void set_y_line_index(int32_t value) { y_line_index_ = value; }
    void advance_line() { ++y_line_index_; }
    void retreat_line() { if (y_line_index_ > 0) --y_line_index_; }

private:
    int32_t y_line_index_ = 0;
};

} // namespace braillatron::kinematics
