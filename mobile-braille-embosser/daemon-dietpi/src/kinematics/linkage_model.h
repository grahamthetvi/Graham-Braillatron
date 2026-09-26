#pragma once

#include "kinematics_config.h"
#include "motion_constants.h"

namespace braillatron::kinematics {

/*
 * Unused by the emboss scheduler. The previous caller added this dwell on
 * top of the 2.5 mm row pitch, which moved Row B before any crank was
 * measured. Keep the helper until a real linkage is on the bench; do not
 * feed it back into EmbossScheduler without that measurement.
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
