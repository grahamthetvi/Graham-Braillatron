#include "kinematics/kinematics_config.h"
#include "kinematics/motion_constants.h"
#include "kinematics/motion_controller.h"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

struct StrikeEvent {
    char row;
    uint8_t mask;
    int64_t travel_microsteps;
};

} // namespace

int main()
{
    using namespace braillatron::kinematics;

    std::vector<StrikeEvent> events;

    KinematicsConfig config {};
    config.spatial_delay_line_capacity = 32;
    config.nominal_carriage_speed_mm_s = 5.0;
    config.tdc_half_angle_deg = 6.0;
    config.crank_radius_mm = 2.0;

    MotionController motion(config);
    motion.set_row_handlers(
        [&](uint8_t mask, int64_t travel) { events.push_back({'A', mask, travel}); },
        [&](uint8_t mask, int64_t travel) { events.push_back({'B', mask, travel}); });

    const uint8_t full_o = (1u << 0) | (1u << 2) | (1u << 4) | (1u << 1) | (1u << 3) | (1u << 5);

    motion.emboss(full_o);

    const uint32_t deferral = motion.row_b_deferral_microsteps();
    for (uint32_t i = 0; i < deferral; ++i) {
        motion.log_carriage_microsteps(1);
    }

    std::cout << "deferral_microsteps=" << deferral << " (base=" << MICROSTEPS_ROW_B_OFFSET
              << ")\n";
    std::cout << "position_mm=" << motion.travel_log().position_mm() << "\n";

    for (const StrikeEvent &event : events) {
        std::cout << "row " << event.row << " mask=0x" << std::hex
                  << static_cast<int>(event.mask) << std::dec
                  << " travel=" << event.travel_microsteps << "\n";
    }

    if (events.size() < 2 || events[0].row != 'A' || events[1].row != 'B') {
        std::cerr << "motion self-test failed\n";
        return 1;
    }

    if (events[1].travel_microsteps - events[0].travel_microsteps !=
        static_cast<int64_t>(deferral)) {
        std::cerr << "row B deferral mismatch\n";
        return 1;
    }

    motion.advance_line_10mm();
    if (motion.travel_log().position_microsteps() !=
        static_cast<int64_t>(deferral + MICROSTEPS_PER_10MM_LINE)) {
        std::cerr << "10 mm advance must be 1600 microsteps\n";
        return 1;
    }

    std::cout << "motion self-test ok\n";
    return 0;
}
