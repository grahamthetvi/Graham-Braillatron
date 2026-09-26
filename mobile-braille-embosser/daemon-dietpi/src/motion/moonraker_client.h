#pragma once

#include "klipper_config.h"

#include <cstdint>
#include <string>

namespace braillatron::motion {

struct EndstopState {
    bool paper_edge = false;
    bool y_home = false;
    bool query_ok = false;
};

class MoonrakerClient {
public:
    explicit MoonrakerClient(KlipperConfig config);

    bool ping();
    bool run_gcode(const std::string &script);
    bool emergency_stop();
    /** Recover from M112 / emergency_stop so G1 moves are accepted again. */
    bool firmware_restart();
    bool home_y();
    bool feed_y_mm(double mm, double speed_mm_s);
    bool move_x_relative_mm(double mm, double speed_mm_s);
    /** Fire one emboss dot via the EMBOSS_DOT macro (printer.cfg). */
    bool emboss_dot(unsigned dot_index, double stroke_mm, double speed_mm_s);
    /** Diagnostic wiggle (~10 s) — Factory Test only, never for embossing. */
    bool stepper_buzz(const std::string &stepper_name);
    EndstopState query_endstops() const;

private:
    std::string get(const std::string &path) const;
    std::string post_json(const std::string &path, const std::string &json_body) const;
    static bool response_ok(const std::string &response);

    KlipperConfig config_;
    bool reachable_ = false;
};

} // namespace braillatron::motion
