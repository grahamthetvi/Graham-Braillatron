#pragma once

#include <cstdint>
#include <string>

namespace braillatron::kinematics {

struct KinematicsConfig {
    uint32_t microsteps_per_full_step = 16;
    double nominal_carriage_speed_mm_s = 5.0;
    double tdc_half_angle_deg = 6.0;
    double crank_radius_mm = 2.0;
    uint32_t spatial_delay_line_capacity = 256;
};

KinematicsConfig load_kinematics_config(const std::string &path);

} // namespace braillatron::kinematics
