#include "travel_log.h"

namespace braillatron::kinematics {

TravelLog::TravelLog()
{
    history_.push_back({0, 0.0});
}

void TravelLog::reset(int64_t position_microsteps)
{
    std::lock_guard<std::mutex> lock(mutex_);
    position_microsteps_ = position_microsteps;
    history_.clear();
    history_.push_back({position_microsteps_, microsteps_to_mm(position_microsteps_)});
}

void TravelLog::log_microsteps(int32_t delta_microsteps)
{
    std::lock_guard<std::mutex> lock(mutex_);
    position_microsteps_ += delta_microsteps;
    history_.push_back({position_microsteps_, microsteps_to_mm(position_microsteps_)});
}

void TravelLog::log_full_steps(int32_t delta_full_steps)
{
    log_microsteps(delta_full_steps * static_cast<int32_t>(MICROSTEPS_PER_FULL_STEP));
}

int64_t TravelLog::position_microsteps() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return position_microsteps_;
}

double TravelLog::position_mm() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return microsteps_to_mm(position_microsteps_);
}

TravelSample TravelLog::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return {position_microsteps_, microsteps_to_mm(position_microsteps_)};
}

std::vector<TravelSample> TravelLog::history() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return history_;
}

} // namespace braillatron::kinematics
