#include "linkage_model.h"

#include <cmath>

namespace braillatron::kinematics {

LinkageModel::LinkageModel(KinematicsConfig config)
    : config_(config)
{
}

double LinkageModel::tdc_dwell_seconds() const
{
    if (config_.nominal_carriage_speed_mm_s <= 0.0 || config_.crank_radius_mm <= 0.0) {
        return 0.0;
    }

    const double half_angle_rad = config_.tdc_half_angle_deg * M_PI / 180.0;
    const double crank_linear_speed =
        config_.nominal_carriage_speed_mm_s / config_.crank_radius_mm;

    /*
     * Small-angle sweep across TDC: delta_t ~= 2 * half_angle / omega.
     * omega is approximated from carriage speed and crank radius.
     */
    return (2.0 * half_angle_rad) / crank_linear_speed;
}

double LinkageModel::tdc_dwell_mm() const
{
    return config_.nominal_carriage_speed_mm_s * tdc_dwell_seconds();
}

uint32_t LinkageModel::tdc_dwell_microsteps() const
{
    return mm_to_microsteps(tdc_dwell_mm());
}

} // namespace braillatron::kinematics
