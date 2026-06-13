#include "timer_service.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace braillatron::ui {

namespace fs = std::filesystem;

namespace {

std::string json_escape(const std::string &value)
{
    std::string out;
    out.reserve(value.size() + 4);
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

int parse_int_field(const std::string &json, const char *key, int default_value)
{
    const std::string needle = std::string("\"") + key + "\":";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return default_value;
    }
    return static_cast<int>(std::strtol(json.c_str() + pos + needle.size(), nullptr, 10));
}

bool parse_bool_field(const std::string &json, const char *key, bool default_value)
{
    const std::string needle = std::string("\"") + key + "\":";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return default_value;
    }
    const size_t start = pos + needle.size();
    if (json.compare(start, 4, "true") == 0) {
        return true;
    }
    if (json.compare(start, 5, "false") == 0) {
        return false;
    }
    return default_value;
}

std::string parse_string_field(const std::string &json, const char *key)
{
    const std::string needle = std::string("\"") + key + "\":\"";
    const size_t pos = json.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    const size_t start = pos + needle.size();
    const size_t end = json.find('"', start);
    if (end == std::string::npos) {
        return {};
    }
    return json.substr(start, end - start);
}

TimerMode parse_mode(const std::string &value)
{
    if (value == "countdown") {
        return TimerMode::Countdown;
    }
    if (value == "stopwatch") {
        return TimerMode::Stopwatch;
    }
    if (value == "pomodoro_work") {
        return TimerMode::PomodoroWork;
    }
    if (value == "pomodoro_break") {
        return TimerMode::PomodoroBreak;
    }
    return TimerMode::Idle;
}

const char *mode_name(TimerMode mode)
{
    switch (mode) {
    case TimerMode::Countdown:
        return "countdown";
    case TimerMode::Stopwatch:
        return "stopwatch";
    case TimerMode::PomodoroWork:
        return "pomodoro_work";
    case TimerMode::PomodoroBreak:
        return "pomodoro_break";
    default:
        return "idle";
    }
}

std::string format_duration(int seconds)
{
    const int minutes = seconds / 60;
    const int secs = seconds % 60;
    if (minutes > 0) {
        return std::to_string(minutes) + " minute" + (minutes == 1 ? "" : "s") + " " +
               std::to_string(secs) + " second" + (secs == 1 ? "" : "s");
    }
    return std::to_string(secs) + " second" + (secs == 1 ? "" : "s");
}

} // namespace

TimerService::TimerService(std::string state_path)
    : state_path_(std::move(state_path))
{
    load_state();
}

void TimerService::set_alert_handler(AlertHandler handler)
{
    alert_handler_ = std::move(handler);
}

void TimerService::set_countdown_minutes(int minutes)
{
    countdown_minutes_ = minutes < 1 ? 1 : minutes;
}

void TimerService::begin_mode(TimerMode mode, int duration_sec)
{
    mode_ = mode;
    duration_sec_ = duration_sec;
    remaining_sec_ = duration_sec;
    elapsed_sec_ = 0;
    active_ = mode != TimerMode::Idle;
    paused_ = false;
    last_tick_ms_ = 0;
    pomodoro_on_break_ = mode == TimerMode::PomodoroBreak;
    save_state();
}

void TimerService::start_countdown()
{
    begin_mode(TimerMode::Countdown, countdown_minutes_ * 60);
}

void TimerService::start_stopwatch()
{
    begin_mode(TimerMode::Stopwatch, 0);
}

void TimerService::start_pomodoro()
{
    begin_mode(TimerMode::PomodoroWork, pomodoro_work_sec_);
}

void TimerService::pause()
{
    if (!active_ || paused_) {
        return;
    }
    paused_ = true;
    save_state();
}

void TimerService::resume()
{
    if (!active_ || !paused_) {
        return;
    }
    paused_ = false;
    last_tick_ms_ = 0;
    save_state();
}

void TimerService::reset()
{
    if (mode_ == TimerMode::Stopwatch) {
        elapsed_sec_ = 0;
    } else {
        remaining_sec_ = duration_sec_;
        elapsed_sec_ = 0;
    }
    paused_ = false;
    last_tick_ms_ = 0;
    save_state();
}

void TimerService::cancel()
{
    mode_ = TimerMode::Idle;
    active_ = false;
    paused_ = false;
    duration_sec_ = 0;
    remaining_sec_ = 0;
    elapsed_sec_ = 0;
    pomodoro_on_break_ = false;
    last_tick_ms_ = 0;
    save_state();
}

void TimerService::advance_pomodoro()
{
    if (pomodoro_on_break_) {
        fire_alert("Pomodoro break finished. Work time.");
        begin_mode(TimerMode::PomodoroWork, pomodoro_work_sec_);
        return;
    }
    fire_alert("Pomodoro work finished. Break time.");
    begin_mode(TimerMode::PomodoroBreak, pomodoro_break_sec_);
}

void TimerService::fire_alert(const std::string &message)
{
    if (alert_handler_) {
        alert_handler_(message);
    }
}

void TimerService::tick(uint64_t now_ms)
{
    if (!active_ || paused_) {
        return;
    }
    if (last_tick_ms_ == 0) {
        last_tick_ms_ = now_ms;
        return;
    }
    if (now_ms <= last_tick_ms_) {
        return;
    }

    const uint64_t delta_ms = now_ms - last_tick_ms_;
    if (delta_ms < 1000) {
        return;
    }

    const int steps = static_cast<int>(delta_ms / 1000);
    last_tick_ms_ += static_cast<uint64_t>(steps) * 1000;

    if (mode_ == TimerMode::Stopwatch) {
        elapsed_sec_ += steps;
        save_state();
        return;
    }

    remaining_sec_ -= steps;
    elapsed_sec_ += steps;
    if (remaining_sec_ > 0) {
        save_state();
        return;
    }

    remaining_sec_ = 0;
    if (mode_ == TimerMode::Countdown) {
        fire_alert("Timer finished.");
        cancel();
        return;
    }
    if (mode_ == TimerMode::PomodoroWork || mode_ == TimerMode::PomodoroBreak) {
        advance_pomodoro();
        return;
    }
}

std::string TimerService::status_text() const
{
    if (!active_) {
        return "Timer idle";
    }
    if (mode_ == TimerMode::Stopwatch) {
        return paused_ ? "Stopwatch paused at " + format_duration(elapsed_sec_)
                       : "Stopwatch " + format_duration(elapsed_sec_);
    }
    const std::string prefix = paused_ ? "Paused. " : "";
    if (mode_ == TimerMode::PomodoroWork) {
        return prefix + "Pomodoro work. " + format_duration(remaining_sec_) + " remaining";
    }
    if (mode_ == TimerMode::PomodoroBreak) {
        return prefix + "Pomodoro break. " + format_duration(remaining_sec_) + " remaining";
    }
    return prefix + "Countdown. " + format_duration(remaining_sec_) + " remaining";
}

void TimerService::save_state() const
{
    std::error_code ec;
    const fs::path path(state_path_);
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
    }

    std::ofstream out(state_path_);
    if (!out.is_open()) {
        return;
    }

    out << "{\n"
        << "  \"mode\":\"" << json_escape(mode_name(mode_)) << "\",\n"
        << "  \"active\":" << (active_ ? "true" : "false") << ",\n"
        << "  \"paused\":" << (paused_ ? "true" : "false") << ",\n"
        << "  \"countdown_minutes\":" << countdown_minutes_ << ",\n"
        << "  \"duration_sec\":" << duration_sec_ << ",\n"
        << "  \"remaining_sec\":" << remaining_sec_ << ",\n"
        << "  \"elapsed_sec\":" << elapsed_sec_ << ",\n"
        << "  \"pomodoro_work_sec\":" << pomodoro_work_sec_ << ",\n"
        << "  \"pomodoro_break_sec\":" << pomodoro_break_sec_ << ",\n"
        << "  \"pomodoro_on_break\":" << (pomodoro_on_break_ ? "true" : "false") << "\n"
        << "}\n";
}

void TimerService::load_state()
{
    std::ifstream in(state_path_);
    if (!in.is_open()) {
        return;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string json = buffer.str();

    mode_ = parse_mode(parse_string_field(json, "mode"));
    active_ = parse_bool_field(json, "active", false);
    paused_ = parse_bool_field(json, "paused", false);
    countdown_minutes_ = parse_int_field(json, "countdown_minutes", countdown_minutes_);
    duration_sec_ = parse_int_field(json, "duration_sec", duration_sec_);
    remaining_sec_ = parse_int_field(json, "remaining_sec", remaining_sec_);
    elapsed_sec_ = parse_int_field(json, "elapsed_sec", elapsed_sec_);
    pomodoro_work_sec_ = parse_int_field(json, "pomodoro_work_sec", pomodoro_work_sec_);
    pomodoro_break_sec_ = parse_int_field(json, "pomodoro_break_sec", pomodoro_break_sec_);
    pomodoro_on_break_ = parse_bool_field(json, "pomodoro_on_break", pomodoro_on_break_);
}

} // namespace braillatron::ui
