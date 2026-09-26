#pragma once

#include "../documents/liblouis_bridge.h"
#include "../kinematics/kinematics_config.h"
#include "../kinematics/motion_controller.h"
#include "../kinematics/paper_position.h"

#include <cstdint>
#include <functional>
#include <string>

namespace braillatron::motion {

class MotionService {
public:
    explicit MotionService(kinematics::KinematicsConfig config);

    braillatron::kinematics::MotionController &controller();
    const braillatron::kinematics::MotionController &controller() const;
    braillatron::kinematics::PaperPosition &paper();
    const braillatron::kinematics::PaperPosition &paper() const;

    void emboss_dot_mask(uint8_t dot_mask);
    void emboss_text(const std::string &plain,
                     const braillatron::documents::BrailleTranslationService &braille);
    /** Emboss North American BRF cells. Does not forward-translate. */
    void emboss_brf(const std::string &brf);
    bool advance_line();
    bool feed_lines(int32_t delta);
    void reset_from_coordinate(int64_t x_microsteps, int32_t y_line_index);

    void set_row_strike_log(std::function<void(uint8_t, int64_t)> logger);
    /**
     * Physical line feed (positive = advance, negative = retreat), installed
     * by KlipperMotionBridge. Responsible for the Y feed and for returning
     * the carriage to the line start (X0).
     */
    void set_line_feed_handler(std::function<bool(int32_t)> handler);

private:
    braillatron::kinematics::MotionController controller_;
    braillatron::kinematics::PaperPosition paper_;
    std::function<void(uint8_t, int64_t)> strike_logger_;
    std::function<bool(int32_t)> line_feed_;
};

} // namespace braillatron::motion
