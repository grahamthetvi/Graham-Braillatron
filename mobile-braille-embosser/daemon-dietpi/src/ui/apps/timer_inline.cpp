#include "app_session.h"
#include "app_registry.h"
#include "app_util.h"
#include "ui_context.h"

#include "../timer_service.h"

#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

enum class SetupPhase {
    PickMode,
    SetDuration,
    Ready,
};

class TimerInline final : public AppSession {
public:
    std::string id() const override { return "timer"; }
    std::string label() const override { return "Timer"; }
    AppKind kind() const override { return AppKind::Inline; }

    void on_enter(UiContext &ctx) override
    {
        phase_ = SetupPhase::PickMode;
        mode_index_ = 0;
        seconds_ = ctx.timer != nullptr && ctx.timer->remaining_seconds() > 0
                       ? ctx.timer->remaining_seconds()
                       : 5 * 60;
        if (seconds_ < 1) {
            seconds_ = 5 * 60;
        }
        announce_mode(ctx);
    }

    void on_exit(UiContext &) override
    {
        phase_ = SetupPhase::PickMode;
    }

    void on_poll(UiContext &) override {}

    void on_chord(uint8_t, UiContext &) override {}

    void on_text(const std::string &, UiContext &) override {}

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed || ctx.timer == nullptr) {
            return;
        }

        if (key == keyboard::ControlKey::Backspace) {
            if (phase_ == SetupPhase::SetDuration) {
                phase_ = SetupPhase::PickMode;
                announce_mode(ctx);
                return;
            }
            if (ctx.registry != nullptr) {
                ctx.registry->exit_inline();
            }
            announce(ctx, "Timer setup closed");
            return;
        }

        if (key == keyboard::ControlKey::DpadUp) {
            if (phase_ == SetupPhase::PickMode && mode_index_ > 0) {
                --mode_index_;
                announce_mode(ctx);
            } else if (phase_ == SetupPhase::SetDuration && seconds_ < 120 * 60) {
                seconds_ = seconds_ < 60 ? seconds_ + 1 : seconds_ + 60;
                if (seconds_ > 120 * 60) {
                    seconds_ = 120 * 60;
                }
                announce_duration(ctx);
            }
            return;
        }

        if (key == keyboard::ControlKey::DpadDown) {
            if (phase_ == SetupPhase::PickMode &&
                mode_index_ + 1 < static_cast<int>(sizeof(kModes) / sizeof(kModes[0]))) {
                ++mode_index_;
                announce_mode(ctx);
            } else if (phase_ == SetupPhase::SetDuration && seconds_ > 1) {
                seconds_ = seconds_ <= 60 ? seconds_ - 1 : seconds_ - 60;
                if (seconds_ < 1) {
                    seconds_ = 1;
                }
                announce_duration(ctx);
            }
            return;
        }

        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        if (phase_ == SetupPhase::PickMode) {
            selected_mode_ = kModes[static_cast<size_t>(mode_index_)];
            if (selected_mode_ == "Countdown") {
                phase_ = SetupPhase::SetDuration;
                announce_duration(ctx);
                return;
            }
            start_selected_mode(ctx);
            return;
        }

        if (phase_ == SetupPhase::SetDuration) {
            start_selected_mode(ctx);
        }
    }

private:
    void announce_mode(UiContext &ctx)
    {
        announce(ctx, std::string("Timer mode. ") + kModes[static_cast<size_t>(mode_index_)] +
                           ". Enter to choose. Back to exit.");
    }

    void announce_duration(UiContext &ctx)
    {
        if (seconds_ < 60) {
            announce(ctx, "Countdown seconds: " + std::to_string(seconds_) +
                               ". Up increases, down decreases. Enter to start.");
            return;
        }
        announce(ctx, "Countdown minutes: " + std::to_string(seconds_ / 60) +
                           ". Up increases, down decreases. Enter to start.");
    }

    void start_selected_mode(UiContext &ctx)
    {
        if (selected_mode_ == "Countdown") {
            ctx.timer->set_countdown_seconds(seconds_);
            ctx.timer->start_countdown();
            if (seconds_ < 60) {
                announce(ctx, "Countdown started for " + std::to_string(seconds_) + " seconds.");
            } else {
                announce(ctx, "Countdown started for " + std::to_string(seconds_ / 60) + " minutes.");
            }
        } else if (selected_mode_ == "Stopwatch") {
            ctx.timer->start_stopwatch();
            announce(ctx, "Stopwatch started.");
        } else {
            ctx.timer->start_pomodoro();
            announce(ctx, "Pomodoro started. 25 minute work, 5 minute break.");
        }

        if (ctx.registry != nullptr) {
            ctx.registry->exit_inline();
        }
    }

    static constexpr const char *kModes[] = {"Countdown", "Stopwatch", "Pomodoro"};
    SetupPhase phase_ = SetupPhase::PickMode;
    int mode_index_ = 0;
    int seconds_ = 5 * 60;
    std::string selected_mode_ = "Countdown";
};

} // namespace

std::unique_ptr<AppSession> make_timer_inline()
{
    return std::make_unique<TimerInline>();
}

} // namespace braillatron::ui
