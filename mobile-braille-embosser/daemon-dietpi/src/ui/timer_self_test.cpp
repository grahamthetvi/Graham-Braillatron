#include "timer_service.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

int failures = 0;

void expect_true(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

std::string temp_state_path()
{
    return "/tmp/braillatron-timer-self-test-" + std::to_string(::getpid()) + ".json";
}

bool test_countdown_alert()
{
    braillatron::ui::TimerService timer(temp_state_path());
    std::string alert;
    timer.set_alert_handler([&alert](const std::string &message) { alert = message; });
    timer.set_countdown_minutes(1);
    timer.start_countdown();
    expect_true(timer.running(), "countdown running");
    expect_true(timer.remaining_seconds() == 60, "countdown initial remaining");

    timer.tick(1000);
    expect_true(timer.remaining_seconds() == 60, "countdown no tick before one second");

    timer.tick(2000);
    expect_true(timer.remaining_seconds() == 59, "countdown decrements after one second");

    timer.tick(61000);
    expect_true(!timer.running(), "countdown finishes");
    expect_true(alert.find("finished") != std::string::npos, "countdown alert fired");
    return true;
}

bool test_stopwatch_and_pause()
{
    braillatron::ui::TimerService timer(temp_state_path());
    timer.start_stopwatch();
    timer.tick(1000);
    timer.tick(6000);
    expect_true(timer.elapsed_seconds() == 5, "stopwatch elapsed");
    timer.pause();
    timer.tick(11000);
    expect_true(timer.elapsed_seconds() == 5, "stopwatch paused");
    timer.resume();
    timer.tick(12000);
    timer.tick(13000);
    expect_true(timer.elapsed_seconds() == 6, "stopwatch resumes");
    return true;
}

bool test_state_persistence()
{
    const std::string path = temp_state_path();
    {
        braillatron::ui::TimerService timer(path);
        timer.set_countdown_minutes(3);
        timer.start_countdown();
        timer.tick(1000);
        timer.tick(5000);
    }

    braillatron::ui::TimerService restored(path);
    expect_true(restored.running(), "restored timer active");
    expect_true(restored.remaining_seconds() == 176, "restored remaining seconds");
    expect_true(restored.status_text().find("Countdown") != std::string::npos,
                "restored status text");

    std::error_code ec;
    std::filesystem::remove(path, ec);
    return true;
}

} // namespace

int main()
{
    test_countdown_alert();
    test_stopwatch_and_pause();
    test_state_persistence();

    if (failures != 0) {
        std::cerr << failures << " timer self-test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "timer self-test passed\n";
    return EXIT_SUCCESS;
}
