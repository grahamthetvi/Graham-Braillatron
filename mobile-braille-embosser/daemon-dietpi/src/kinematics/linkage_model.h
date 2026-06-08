#pragma once

#include "kinematics_config.h"
#include "motion_constants.h"

namespace braillatron::kinematics {

/*
 * Slider-crank dwell near Top Dead Center (.cursorrules §3.4):
 *   F_pin = tau / (r * cos(theta))  ->  as theta -> 90 deg, cos(theta) -> 0.
 * Horizontal carriage advance continues while the pin dwells at TDC impact.
 * Equivalent extra deferral is converted to microsteps at nominal carriage speed.
 */
class LinkageModel {
public:
    explicit LinkageModel(KinematicsConfig config);

    double tdc_dwell_seconds() const;
    double tdc_dwell_mm() const;
    uint32_t tdc_dwell_microsteps() const;

private:
    KinematicsConfig config_;
};

} // namespace braillatron::kinematics
