#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace braillatron::ui {

enum class TimerMode {
    Idle,
    Countdown,
    Stopwatch,
    PomodoroWork,
    PomodoroBreak,
};

class TimerService {
public:
    using AlertHandler = std::function<void(const std::string &message)>;

    explicit TimerService(std::string state_path = "/data/braillatron/timer/state.json");

    void set_alert_handler(AlertHandler handler);

    void tick(uint64_t now_ms);
    bool running() const { return active_; }
    TimerMode mode() const { return mode_; }
    int remaining_seconds() const { return remaining_sec_; }
    int elapsed_seconds() const { return elapsed_sec_; }

    void set_countdown_minutes(int minutes);
    void start_countdown();
    void start_stopwatch();
    void start_pomodoro();
    void pause();
    void resume();
    void reset();
    void cancel();

    std::string status_text() const;
    void save_state() const;
    void load_state();

private:
    void fire_alert(const std::string &message);
    void begin_mode(TimerMode mode, int duration_sec);
    void advance_pomodoro();

    std::string state_path_;
    AlertHandler alert_handler_;
    TimerMode mode_ = TimerMode::Idle;
    bool active_ = false;
    bool paused_ = false;
    uint64_t last_tick_ms_ = 0;
    int countdown_minutes_ = 5;
    int duration_sec_ = 0;
    int remaining_sec_ = 0;
    int elapsed_sec_ = 0;
    int pomodoro_work_sec_ = 25 * 60;
    int pomodoro_break_sec_ = 5 * 60;
    bool pomodoro_on_break_ = false;
};

} // namespace braillatron::ui
