#pragma once

#include <cstdint>
#include <string>

namespace braillatron::motion {

struct KlipperConfig {
    bool enabled = false;
    std::string moonraker_url = "http://127.0.0.1:7125";
    uint32_t request_timeout_sec = 5;

    double y_feed_mm_per_line = 10.0;
    double y_feed_speed_mm_s = 20.0;
    double x_move_speed_mm_s = 30.0;
    uint32_t stepper_buzz_duration_ms = 100;

    std::string emboss_stepper_1 = "emboss_1";
    std::string emboss_stepper_2 = "emboss_2";
    std::string emboss_stepper_3 = "emboss_3";
    std::string emboss_stepper_4 = "emboss_4";
    std::string emboss_stepper_5 = "emboss_5";
    std::string emboss_stepper_6 = "emboss_6";

    std::string emboss_stepper_name(unsigned dot_index) const;
};

KlipperConfig load_klipper_config(const std::string &path);

} // namespace braillatron::motion
