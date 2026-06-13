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
    SetMinutes,
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
        minutes_ = ctx.timer != nullptr && ctx.timer->remaining_seconds() > 0
                       ? ctx.timer->remaining_seconds() / 60
                       : 5;
        if (minutes_ < 1) {
            minutes_ = 5;
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
            if (phase_ == SetupPhase::SetMinutes) {
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
            } else if (phase_ == SetupPhase::SetMinutes && minutes_ < 120) {
                ++minutes_;
                announce_minutes(ctx);
            }
            return;
        }

        if (key == keyboard::ControlKey::DpadDown) {
            if (phase_ == SetupPhase::PickMode &&
                mode_index_ + 1 < static_cast<int>(sizeof(kModes) / sizeof(kModes[0]))) {
                ++mode_index_;
                announce_mode(ctx);
            } else if (phase_ == SetupPhase::SetMinutes && minutes_ > 1) {
                --minutes_;
                announce_minutes(ctx);
            }
            return;
        }

        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        if (phase_ == SetupPhase::PickMode) {
            selected_mode_ = kModes[static_cast<size_t>(mode_index_)];
            if (selected_mode_ == "Countdown") {
                phase_ = SetupPhase::SetMinutes;
                announce_minutes(ctx);
                return;
            }
            start_selected_mode(ctx);
            return;
        }

        if (phase_ == SetupPhase::SetMinutes) {
            start_selected_mode(ctx);
        }
    }

private:
    void announce_mode(UiContext &ctx)
    {
        announce(ctx, std::string("Timer mode. ") + kModes[static_cast<size_t>(mode_index_)] +
                           ". Enter to choose. Back to exit.");
    }

    void announce_minutes(UiContext &ctx)
    {
        announce(ctx, "Countdown minutes: " + std::to_string(minutes_) +
                           ". Up increases, down decreases. Enter to start.");
    }

    void start_selected_mode(UiContext &ctx)
    {
        if (selected_mode_ == "Countdown") {
            ctx.timer->set_countdown_minutes(minutes_);
            ctx.timer->start_countdown();
            announce(ctx, "Countdown started for " + std::to_string(minutes_) + " minutes.");
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
    int minutes_ = 5;
    std::string selected_mode_ = "Countdown";
};

} // namespace

std::unique_ptr<AppSession> make_timer_inline()
{
    return std::make_unique<TimerInline>();
}

} // namespace braillatron::ui
