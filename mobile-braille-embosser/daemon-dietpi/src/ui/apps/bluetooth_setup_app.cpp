#include "app_registry.h"
#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../../platform/audio_output.h"
#include "../../platform/shell_util.h"

#include <memory>
#include <string>

namespace braillatron::ui {
namespace {

class BluetoothSetupApp final : public AppSession {
public:
    std::string id() const override { return "bluetooth_setup"; }
    std::string label() const override { return "Pair Bluetooth"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        mac_buffer_.clear();
        announce(ctx,
                 "Enter speaker MAC address. Colons optional. Press Enter when done. Backspace "
                 "deletes.");
    }

    void on_exit(UiContext &ctx) override
    {
        mac_buffer_.clear();
        announce(ctx, "Bluetooth pairing closed");
    }

    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}

    void on_text(const std::string &text, UiContext &ctx) override
    {
        if (text.empty()) {
            return;
        }
        mac_buffer_ += text;
        if (mac_buffer_.size() % 4 == 0) {
            announce(ctx, std::to_string(mac_buffer_.size()) + " characters entered");
        }
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed) {
            return;
        }

        if (key == keyboard::ControlKey::Backspace) {
            if (!mac_buffer_.empty()) {
                mac_buffer_.pop_back();
            }
            if (mac_buffer_.size() % 4 == 0 && !mac_buffer_.empty()) {
                announce(ctx, std::to_string(mac_buffer_.size()) + " characters entered");
            }
            return;
        }

        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        const auto normalized = platform::normalize_mac(mac_buffer_);
        if (!normalized.has_value()) {
            announce(ctx, "Invalid MAC address. Use twelve hex digits.");
            return;
        }

        if (!platform::save_bluetooth_mac(*normalized)) {
            announce(ctx, "Could not save Bluetooth speaker address.");
            return;
        }

        const std::string pair_result = platform::pair_bluetooth_mac(*normalized);
        announce(ctx, pair_result);

        const std::string switch_result = platform::switch_output("bluetooth");
        announce(ctx, switch_result);

        if (ctx.registry != nullptr) {
            ctx.registry->exit();
        }
    }

private:
    std::string mac_buffer_;
};

} // namespace

std::unique_ptr<AppSession> make_bluetooth_setup_app()
{
    return std::make_unique<BluetoothSetupApp>();
}

} // namespace braillatron::ui
