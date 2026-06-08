#include "spatial_delay_line.h"

#include <stdexcept>

namespace braillatron::kinematics {

SpatialDelayLine::SpatialDelayLine(uint32_t capacity)
{
    if (capacity == 0) {
        throw std::invalid_argument("spatial delay line capacity must be > 0");
    }
    ring_.resize(capacity);
}

void SpatialDelayLine::enqueue(DeferredRowBStrike entry)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ >= ring_.size()) {
        throw std::overflow_error("spatial delay line capacity exceeded");
    }

    entry.sequence = next_sequence_++;
    ring_[tail_] = entry;
    tail_ = (tail_ + 1) % ring_.size();
    ++count_;
}

std::vector<DeferredRowBStrike> SpatialDelayLine::drain_due(int64_t current_position_microsteps)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DeferredRowBStrike> due;

    while (count_ > 0) {
        const DeferredRowBStrike &candidate = ring_[head_];
        if (candidate.fire_at_microsteps > current_position_microsteps) {
            break;
        }

        due.push_back(candidate);
        head_ = (head_ + 1) % ring_.size();
        --count_;
    }

    return due;
}

size_t SpatialDelayLine::pending_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

} // namespace braillatron::kinematics
