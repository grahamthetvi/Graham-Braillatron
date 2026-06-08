#pragma once

#include "motion_constants.h"

#include <cstdint>
#include <mutex>
#include <vector>

namespace braillatron::kinematics {

struct TravelSample {
    int64_t position_microsteps;
    double position_mm;
};

/*
 * Thread-safe cumulative tractor/carriage step log at 0.1 mm/full-step resolution.
 */
class TravelLog {
public:
    TravelLog();

    void reset(int64_t position_microsteps = 0);
    void log_microsteps(int32_t delta_microsteps);
    void log_full_steps(int32_t delta_full_steps);

    int64_t position_microsteps() const;
    double position_mm() const;
    TravelSample snapshot() const;

    std::vector<TravelSample> history() const;

private:
    mutable std::mutex mutex_;
    int64_t position_microsteps_ = 0;
    std::vector<TravelSample> history_;
};

} // namespace braillatron::kinematics
