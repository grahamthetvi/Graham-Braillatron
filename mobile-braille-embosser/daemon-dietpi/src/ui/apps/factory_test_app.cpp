#include "app_session.h"
#include "ui_context.h"

#include "../../connect/subprocess.h"
#include "../../keyboard/keyboard_service.h"
#include "../../motion/klipper_motion_bridge.h"
#include "../../motion/moonraker_client.h"
#include "../../motion_gate.h"
#include "../../telemetry/telemetry_bridge.h"
#include "../layered_browse_list.h"
#include "../output_hub.h"

extern "C" {
#include "protocol.h"
}

#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

enum class Phase {
    PinEntry,
    Menu,
    Running,
};

enum class TestKind {
    MotorX,
    MotorY,
    MotorDot1,
    MotorDot2,
    MotorDot3,
    MotorDot4,
    MotorDot5,
    MotorDot6,
    Speaker440,
    ArduinoButtons,
    BatteryStatus,
    ChargingState,
    Temperature,
    PaperSensors,
    Haptics,
    MotionGate,
};

struct TestItem {
    TestKind kind;
    std::string label;
};

void journal_log(const std::string &message)
{
    std::cerr << "[factory] " << message << "\n";
    std::fprintf(stderr, "%s\n", message.c_str());
}

class FactoryTestApp final : public AppSession {
public:
    std::string id() const override { return "factory_test"; }
    std::string label() const override { return "Factory Test"; }
    AppKind kind() const override { return AppKind::Standalone; }

    bool browse_list_active() const override
    {
        return unlocked_ && (phase_ == Phase::Menu || phase_ == Phase::Running);
    }

    const LayeredBrowseList *browse_list() const override
    {
        return browse_list_active() ? &browse_ : nullptr;
    }

    std::string composer_line() const override
    {
        if (phase_ == Phase::PinEntry) {
            return pin_buffer_;
        }
        if (!last_result_.empty()) {
            return last_result_;
        }
        return {};
    }

    std::vector<std::string> browse_items() const override
    {
        return browse_.labels();
    }

    size_t browse_focus_index() const override
    {
        return browse_.focus_index();
    }

    bool buffers_braille_words() const override { return phase_ == Phase::PinEntry; }
    bool masks_typing_echo() const override { return phase_ == Phase::PinEntry; }

    void on_enter(UiContext &ctx) override
    {
        ctx_ = &ctx;
        reset_session(ctx);
        if (ctx.dev_mode) {
            unlocked_ = true;
            phase_ = Phase::Menu;
            build_menu();
            announce(ctx, "Factory test. Dev mode — PIN not required.");
        } else {
            phase_ = Phase::PinEntry;
            announce(ctx, "Factory test. Enter PIN.");
        }
        sync_chrome(ctx);
    }

    void on_exit(UiContext &ctx) override
    {
        (void)ctx;
        ctx_ = nullptr;
    }

    void on_poll(UiContext &ctx) override
    {
        (void)ctx;
    }

    void on_chord(uint8_t, UiContext &) override {}

    void on_text(const std::string &text, UiContext &ctx) override
    {
        if (phase_ != Phase::PinEntry) {
            return;
        }

        for (char ch : text) {
            if (ch >= '0' && ch <= '9') {
                pin_buffer_.push_back(ch);
            }
        }

        if (pin_buffer_.size() >= ctx.factory_pin.size()) {
            if (pin_buffer_ == ctx.factory_pin) {
                unlocked_ = true;
                phase_ = Phase::Menu;
                pin_buffer_.clear();
                build_menu();
                announce(ctx, "PIN accepted. Factory test menu.");
            } else {
                pin_buffer_.clear();
                announce(ctx, "Incorrect PIN.");
            }
        }
        sync_chrome(ctx);
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed || !unlocked_) {
            return;
        }

        if (phase_ == Phase::Menu || phase_ == Phase::Running) {
            if (key == keyboard::ControlKey::DpadUp) {
                browse_.move_up();
                sync_chrome(ctx);
                return;
            }
            if (key == keyboard::ControlKey::DpadDown) {
                browse_.move_down();
                sync_chrome(ctx);
                return;
            }
            if (key == keyboard::ControlKey::Enter) {
                run_selected_test(ctx);
                return;
            }
        }
    }

private:
    void reset_session(UiContext &ctx)
    {
        (void)ctx;
        phase_ = Phase::PinEntry;
        unlocked_ = false;
        pin_buffer_.clear();
        last_result_.clear();
        browse_.clear();
    }

    void build_menu()
    {
        const std::vector<TestItem> items {
            {TestKind::MotorX, "Motor X carriage"},
            {TestKind::MotorY, "Motor Y feed"},
            {TestKind::MotorDot1, "Motor emboss dot 1"},
            {TestKind::MotorDot2, "Motor emboss dot 2"},
            {TestKind::MotorDot3, "Motor emboss dot 3"},
            {TestKind::MotorDot4, "Motor emboss dot 4"},
            {TestKind::MotorDot5, "Motor emboss dot 5"},
            {TestKind::MotorDot6, "Motor emboss dot 6"},
            {TestKind::Speaker440, "Speaker 440 hertz sine"},
            {TestKind::ArduinoButtons, "Arduino button matrix"},
            {TestKind::BatteryStatus, "Battery percent"},
            {TestKind::ChargingState, "Charging state"},
            {TestKind::Temperature, "LTC2944 temperature"},
            {TestKind::PaperSensors, "Paper endstops (Klipper)"},
            {TestKind::Haptics, "DRV2605L haptic pulse"},
            {TestKind::MotionGate, "Motion gate status"},
        };

        std::vector<std::string> labels;
        labels.reserve(items.size());
        for (const TestItem &item : items) {
            labels.push_back(item.label);
        }
        browse_.set_items(labels);
        test_items_ = items;
    }

    void announce(UiContext &ctx, const std::string &message)
    {
        journal_log(message);
        if (ctx.output != nullptr) {
            ctx.output->announce_message(message);
        }
    }

    void sync_chrome(UiContext &ctx)
    {
        if (ctx.output != nullptr) {
            ctx.output->sync_chrome(false);
        }
    }

    void run_selected_test(UiContext &ctx)
    {
        const size_t index = browse_.focus_index();
        if (index >= test_items_.size()) {
            return;
        }

        if (MotionGate::is_blocked() && test_items_[index].kind != TestKind::MotionGate &&
            test_items_[index].kind != TestKind::ArduinoButtons &&
            test_items_[index].kind != TestKind::BatteryStatus &&
            test_items_[index].kind != TestKind::ChargingState &&
            test_items_[index].kind != TestKind::Temperature &&
            test_items_[index].kind != TestKind::Speaker440) {
            announce(ctx, "Motion blocked. Test skipped.");
            last_result_ = "Blocked: " + std::string(MotionGate::block_reason());
            sync_chrome(ctx);
            return;
        }

        phase_ = Phase::Running;
        const TestKind kind = test_items_[index].kind;
        switch (kind) {
        case TestKind::MotorX:
            run_motor(ctx, "stepper_x");
            break;
        case TestKind::MotorY:
            run_motor(ctx, "stepper_y");
            break;
        case TestKind::MotorDot1:
            run_motor(ctx, "emboss_1");
            break;
        case TestKind::MotorDot2:
            run_motor(ctx, "emboss_2");
            break;
        case TestKind::MotorDot3:
            run_motor(ctx, "emboss_3");
            break;
        case TestKind::MotorDot4:
            run_motor(ctx, "emboss_4");
            break;
        case TestKind::MotorDot5:
            run_motor(ctx, "emboss_5");
            break;
        case TestKind::MotorDot6:
            run_motor(ctx, "emboss_6");
            break;
        case TestKind::Speaker440:
            run_speaker(ctx);
            break;
        case TestKind::ArduinoButtons:
            run_buttons(ctx);
            break;
        case TestKind::BatteryStatus:
            run_battery(ctx);
            break;
        case TestKind::ChargingState:
            run_charging(ctx);
            break;
        case TestKind::Temperature:
            run_temperature(ctx);
            break;
        case TestKind::PaperSensors:
            run_paper_sensors(ctx);
            break;
        case TestKind::Haptics:
            run_haptics(ctx);
            break;
        case TestKind::MotionGate:
            run_motion_gate(ctx);
            break;
        }

        phase_ = Phase::Menu;
        sync_chrome(ctx);
    }

    void run_motor(UiContext &ctx, const std::string &stepper)
    {
        if (ctx.klipper == nullptr || !ctx.klipper->is_ready()) {
            last_result_ = stepper + ": Klipper unavailable";
            announce(ctx, last_result_);
            return;
        }

        const bool ok = ctx.klipper->client().stepper_buzz(stepper, 100u);
        last_result_ = stepper + (ok ? ": STEPPER_BUZZ ok" : ": STEPPER_BUZZ failed");
        announce(ctx, last_result_);
    }

    void run_speaker(UiContext &ctx)
    {
        const int status = braillatron::connect::run_command_status(
            "speaker-test -t sine -f 440 -c 1 -l 1 2>/dev/null");
        last_result_ = status == 0 ? "Speaker 440 hertz: ok" : "Speaker 440 hertz: failed";
        announce(ctx, last_result_);
    }

    void run_buttons(UiContext &ctx)
    {
        if (ctx.keyboard == nullptr) {
            last_result_ = "Arduino buttons: keyboard service unavailable";
            announce(ctx, last_result_);
            return;
        }

        const uint16_t state = ctx.keyboard->last_matrix_state();
        std::ostringstream stream;
        stream << "Button matrix 0x" << std::hex << state << std::dec;
        last_result_ = stream.str();
        announce(ctx, "Arduino buttons " + last_result_);
    }

    void run_battery(UiContext &ctx)
    {
        const telemetry::TelemetrySnapshot snap =
            telemetry::read_telemetry_json(telemetry::kTelemetryJsonPath);
        if (snap.battery_percent == BRAILLATRON_TELEMETRY_UNKNOWN) {
            last_result_ = "Battery: unread";
        } else {
            last_result_ = "Battery: " + std::to_string(static_cast<unsigned>(snap.battery_percent)) +
                           " percent";
        }
        announce(ctx, last_result_);
    }

    void run_charging(UiContext &ctx)
    {
        (void)ctx;
        const telemetry::TelemetrySnapshot snap =
            telemetry::read_telemetry_json(telemetry::kTelemetryJsonPath);
        last_result_ = snap.charging ? "Charging: yes" : "Charging: no";
        announce(ctx, last_result_);
    }

    void run_temperature(UiContext &ctx)
    {
        (void)ctx;
        const telemetry::TelemetrySnapshot snap =
            telemetry::read_telemetry_json(telemetry::kTelemetryJsonPath);
        if (snap.temperature_c == BRAILLATRON_TELEMETRY_UNKNOWN_S8) {
            last_result_ = "Temperature: unread";
        } else {
            last_result_ = "Temperature: " + std::to_string(static_cast<int>(snap.temperature_c)) +
                           " C";
        }
        announce(ctx, last_result_);
    }

    void run_paper_sensors(UiContext &ctx)
    {
        if (ctx.klipper == nullptr || !ctx.klipper->is_ready()) {
            last_result_ = "Paper sensors: Klipper unavailable";
            announce(ctx, last_result_);
            return;
        }

        const motion::EndstopState endstops = ctx.klipper->client().query_endstops();
        last_result_ = std::string("Paper edge: ") + (endstops.paper_edge ? "triggered" : "open") +
                       ", Y home: " + (endstops.y_home ? "triggered" : "open");
        announce(ctx, last_result_);
    }

    void run_haptics(UiContext &ctx)
    {
        if (ctx.output == nullptr) {
            last_result_ = "Haptics: output hub unavailable";
            announce(ctx, last_result_);
            return;
        }
        ctx.output->play_boundary_haptic();
        last_result_ = "Haptics: boundary pulse sent";
        announce(ctx, last_result_);
    }

    void run_motion_gate(UiContext &ctx)
    {
        if (MotionGate::is_blocked()) {
            last_result_ = std::string("Motion gate blocked: ") + MotionGate::block_reason();
        } else {
            last_result_ = "Motion gate: clear";
        }
        announce(ctx, last_result_);
    }

    UiContext *ctx_ = nullptr;
    Phase phase_ = Phase::PinEntry;
    bool unlocked_ = false;
    std::string pin_buffer_;
    std::string last_result_;
    LayeredBrowseList browse_;
    std::vector<TestItem> test_items_;
};

} // namespace

std::unique_ptr<AppSession> make_factory_test_app()
{
    return std::make_unique<FactoryTestApp>();
}

} // namespace braillatron::ui
