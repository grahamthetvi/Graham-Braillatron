#include "moonraker_client.h"

#include "../connect/json_utils.h"
#include "../connect/subprocess.h"

#include <cmath>
#include <iostream>
#include <sstream>

namespace braillatron::motion {

namespace {

std::string shell_quote(const std::string &value)
{
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

} // namespace

MoonrakerClient::MoonrakerClient(KlipperConfig config)
    : config_(std::move(config))
{
}

bool MoonrakerClient::response_ok(const std::string &response)
{
    return !response.empty() &&
           (response.find("\"result\"") != std::string::npos ||
            response.find("\"ok\": true") != std::string::npos ||
            response.find("\"status\"") != std::string::npos);
}

std::string MoonrakerClient::get(const std::string &path) const
{
    const std::string cmd =
        "curl -fsS --max-time " + std::to_string(config_.request_timeout_sec) + " " +
        shell_quote(config_.moonraker_url + path) + " 2>/dev/null";
    return braillatron::connect::run_command(cmd);
}

std::string MoonrakerClient::post_json(const std::string &path,
                                       const std::string &json_body) const
{
    const std::string cmd =
        "curl -fsS --max-time " + std::to_string(config_.request_timeout_sec) +
        " -X POST -H 'Content-Type: application/json' -d " + shell_quote(json_body) + " " +
        shell_quote(config_.moonraker_url + path) + " 2>/dev/null";
    return braillatron::connect::run_command(cmd);
}

bool MoonrakerClient::ping()
{
    if (!config_.enabled) {
        reachable_ = false;
        return false;
    }

    const std::string response = get("/server/info");
    reachable_ = response_ok(response);
    return reachable_;
}

bool MoonrakerClient::run_gcode(const std::string &script)
{
    if (!config_.enabled) {
        return false;
    }

    const std::string body =
        std::string("{\"script\":") + braillatron::connect::json_escape(script) + "}";
    const std::string response = post_json("/printer/gcode/script", body);
    return response_ok(response);
}

bool MoonrakerClient::emergency_stop()
{
    if (!config_.enabled) {
        return false;
    }

    const std::string response = post_json("/printer/emergency_stop", "{}");
    if (response_ok(response)) {
        return true;
    }
    return run_gcode("M112");
}

bool MoonrakerClient::firmware_restart()
{
    if (!config_.enabled) {
        return false;
    }

    const std::string response = post_json("/printer/firmware_restart", "{}");
    return response_ok(response);
}

bool MoonrakerClient::home_y()
{
    return run_gcode("G28 Y");
}

bool MoonrakerClient::feed_y_mm(double mm, double speed_mm_s)
{
    if (std::abs(mm) < 0.001) {
        return true;
    }

    std::ostringstream script;
    script << "G91\nG1 Y" << mm << " F" << static_cast<int>(speed_mm_s * 60.0) << "\nG90";
    return run_gcode(script.str());
}

bool MoonrakerClient::move_x_relative_mm(double mm, double speed_mm_s)
{
    if (std::abs(mm) < 0.001) {
        return true;
    }

    std::ostringstream script;
    script << "G91\nG1 X" << mm << " F" << static_cast<int>(speed_mm_s * 60.0) << "\nG90";
    return run_gcode(script.str());
}

bool MoonrakerClient::emboss_dot(unsigned dot_index, double stroke_mm, double speed_mm_s)
{
    if (dot_index < 1 || dot_index > 6) {
        return false;
    }

    std::ostringstream script;
    script << "EMBOSS_DOT DOT=" << dot_index << " STROKE=" << stroke_mm
           << " SPEED=" << speed_mm_s;
    return run_gcode(script.str());
}

bool MoonrakerClient::stepper_buzz(const std::string &stepper_name)
{
    // Klipper registers STEPPER_BUZZ targets under the full config section
    // name, so manual steppers must be addressed as "manual_stepper <name>".
    // Quoting is safe: STEPPER_BUZZ is an extended g-code command (shlex).
    std::string full_name = stepper_name;
    if (full_name.find(' ') == std::string::npos &&
        full_name.rfind("stepper_", 0) != 0) {
        full_name = "manual_stepper " + full_name;
    }

    std::ostringstream script;
    script << "STEPPER_BUZZ STEPPER=\"" << full_name << "\"";
    return run_gcode(script.str());
}

bool endstop_value_triggered(const std::string &response, const char *name)
{
    const std::string needle = std::string("\"") + name + "\":";
    const size_t pos = response.find(needle);
    if (pos == std::string::npos) {
        return false;
    }

    size_t value_start = pos + needle.size();
    while (value_start < response.size() &&
           (response[value_start] == ' ' || response[value_start] == '\t')) {
        ++value_start;
    }
    if (value_start < response.size() && response[value_start] == '"') {
        ++value_start;
    }

    if (response.compare(value_start, 4, "true") == 0 ||
        response.compare(value_start, 9, "TRIGGERED") == 0) {
        return true;
    }
    return value_start < response.size() && response[value_start] == '1';
}

EndstopState MoonrakerClient::query_endstops() const
{
    EndstopState state {};
    if (!config_.enabled) {
        return state;
    }

    // The query_endstops printer object only refreshes after a QUERY_ENDSTOPS
    // g-code, so issue one before reading last_query.
    {
        const std::string body = "{\"script\":\"QUERY_ENDSTOPS\"}";
        const std::string gcode_response = post_json("/printer/gcode/script", body);
        if (!response_ok(gcode_response)) {
            return state;
        }
    }

    const std::string response = get("/printer/objects/query?query_endstops");
    if (!response_ok(response)) {
        return state;
    }

    state.query_ok = true;

    // Klipper reports "TRIGGERED" / "open". A boolean payload is accepted too.
    state.y_home = endstop_value_triggered(response, "y");
    // Paper edge (TCRT5000) is wired to Monster8 X-STOP ^PA14 and configured
    // as the stepper_x endstop; QUERY_ENDSTOPS reports it as rail "x".
    // Not E0-STOP / FIL_RUNOUT. X is never homed with G28.
    state.paper_edge = endstop_value_triggered(response, "x");
    return state;
}

} // namespace braillatron::motion
