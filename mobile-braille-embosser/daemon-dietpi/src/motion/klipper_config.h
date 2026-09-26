#pragma once

#include <cstdint>
#include <string>

namespace braillatron::motion {

struct KlipperConfig {
    bool enabled = false;
    std::string moonraker_url = "http://127.0.0.1:7125";
    // Must exceed the longest blocking g-code (STEPPER_BUZZ runs ~10 s).
    uint32_t request_timeout_sec = 15;

    double y_feed_mm_per_line = 10.0;
    double y_feed_speed_mm_s = 20.0;
    double x_move_speed_mm_s = 30.0;

    // EMBOSS_DOT strike stroke and speed (printer.cfg macro parameters).
    double emboss_stroke_mm = 2.0;
    double emboss_speed_mm_s = 40.0;
};

KlipperConfig load_klipper_config(const std::string &path);

} // namespace braillatron::motion
