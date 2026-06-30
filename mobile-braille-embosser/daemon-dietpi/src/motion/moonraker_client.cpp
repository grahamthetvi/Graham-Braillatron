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

bool MoonrakerClient::stepper_buzz(const std::string &stepper_name, uint32_t duration_ms)
{
    std::ostringstream script;
    script << "STEPPER_BUZZ STEPPER=" << stepper_name << " DURATION=" << duration_ms;
    return run_gcode(script.str());
}

EndstopState MoonrakerClient::query_endstops() const
{
    EndstopState state {};
    if (!config_.enabled) {
        return state;
    }

    const std::string response = get("/printer/objects/query?endstops");
    if (!response_ok(response)) {
        return state;
    }

    state.query_ok = true;

    const auto endstop_triggered = [&](const char *name) {
        const std::string needle = std::string("\"") + name + "\": \"";
        const size_t pos = response.find(needle);
        if (pos == std::string::npos) {
            return false;
        }
        const size_t value_start = pos + needle.size();
        return response.compare(value_start, 9, "TRIGGERED") == 0;
    };

    state.y_home = endstop_triggered("y") || endstop_triggered("stepper_y");
    state.paper_edge = endstop_triggered("x") || endstop_triggered("paper_edge");
    return state;
}

} // namespace braillatron::motion
