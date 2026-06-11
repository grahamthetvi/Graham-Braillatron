#pragma once

#include <cstdint>
#include <string>

namespace braillatron::documents {

struct CoordinateState {
    int64_t x_microsteps = 0;
    int32_t y_line_index = 0;
    std::string active_app_id;
    std::string brf_path;
};

class CoordinateStore {
public:
    explicit CoordinateStore(std::string ram_path);

    const CoordinateState &state() const { return state_; }
    CoordinateState &mutable_state() { return state_; }

    bool load();
    bool save() const;

private:
    std::string ram_path_;
    CoordinateState state_;
};

} // namespace braillatron::documents
