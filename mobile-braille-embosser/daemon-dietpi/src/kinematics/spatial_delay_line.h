#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace braillatron::kinematics {

struct DeferredRowBStrike {
    int64_t row_a_strike_microsteps = 0;
    int64_t fire_at_microsteps = 0;
    uint8_t row_b_pin_mask = 0;
    uint64_t sequence = 0;
};

/*
 * Thread-safe circular delay line for Row B emboss pulses deferred in space.
 */
class SpatialDelayLine {
public:
    explicit SpatialDelayLine(uint32_t capacity);

    void enqueue(DeferredRowBStrike entry);
    std::vector<DeferredRowBStrike> drain_due(int64_t current_position_microsteps);

    size_t pending_count() const;

private:
    mutable std::mutex mutex_;
    std::vector<DeferredRowBStrike> ring_;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
    uint64_t next_sequence_ = 1;
};

} // namespace braillatron::kinematics
