#include "app_registry.h"
#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../../platform/audio_output.h"

#include <cctype>
#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

std::string to_lower_ascii(std::string value)
{
    for (char &ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::optional<std::string> resolve_bluetooth_selection(
    const std::string &selection, const std::vector<platform::BluetoothDevice> &devices)
{
    const std::string trimmed = [&selection]() {
        size_t start = 0;
        while (start < selection.size() &&
               std::isspace(static_cast<unsigned char>(selection[start]))) {
            ++start;
        }
        size_t end = selection.size();
        while (end > start && std::isspace(static_cast<unsigned char>(selection[end - 1]))) {
            --end;
        }
        return selection.substr(start, end - start);
    }();

    if (trimmed.empty()) {
        return std::nullopt;
    }

    if (const auto mac = platform::normalize_mac(trimmed); mac.has_value()) {
        return mac;
    }

    const std::string needle = to_lower_ascii(trimmed);
    const platform::BluetoothDevice *exact = nullptr;
    const platform::BluetoothDevice *partial = nullptr;
    int partial_count = 0;

    for (const auto &device : devices) {
        const std::string name = to_lower_ascii(device.name);
        if (name == needle) {
            exact = &device;
            break;
        }
        if (name.find(needle) != std::string::npos) {
            partial = &device;
            ++partial_count;
        }
    }

    if (exact != nullptr) {
        return exact->mac;
    }
    if (partial_count == 1 && partial != nullptr) {
        return partial->mac;
    }
    return std::nullopt;
}

class BluetoothSetupApp final : public AppSession {
public:
    std::string id() const override { return "bluetooth_setup"; }
    std::string label() const override { return "Pair Bluetooth"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        selection_buffer_.clear();
        announce(ctx, "Scanning Bluetooth devices. Put speaker in pairing mode.");
        scanned_devices_ = platform::scan_bluetooth_devices();
        if (scanned_devices_.empty()) {
            announce(ctx, "No devices found. Type MAC address and press Enter.");
        } else {
            int count = 0;
            for (const auto &device : scanned_devices_) {
                if (count >= 10) {
                    break;
                }
                announce(ctx, device.name);
                ++count;
            }
            announce(ctx, "Type device name or MAC address. Press Enter to pair.");
        }
    }

    void on_exit(UiContext &ctx) override
    {
        selection_buffer_.clear();
        scanned_devices_.clear();
        announce(ctx, "Bluetooth pairing closed");
    }

    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}

    void on_text(const std::string &text, UiContext &ctx) override
    {
        if (text.empty()) {
            return;
        }
        selection_buffer_ += text;
        if (selection_buffer_.size() % 4 == 0) {
            announce(ctx, std::to_string(selection_buffer_.size()) + " characters entered");
        }
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed) {
            return;
        }

        if (key == keyboard::ControlKey::Backspace) {
            if (!selection_buffer_.empty()) {
                selection_buffer_.pop_back();
            }
            if (selection_buffer_.size() % 4 == 0 && !selection_buffer_.empty()) {
                announce(ctx, std::to_string(selection_buffer_.size()) + " characters entered");
            }
            return;
        }

        if (key != keyboard::ControlKey::Enter) {
            return;
        }

        const auto mac = resolve_bluetooth_selection(selection_buffer_, scanned_devices_);
        if (!mac.has_value()) {
            announce(ctx, "Device not found. Check name or MAC address.");
            return;
        }

        if (!platform::save_bluetooth_mac(*mac)) {
            announce(ctx, "Could not save Bluetooth speaker address.");
            return;
        }

        const std::string pair_result = platform::pair_bluetooth_mac(*mac);
        announce(ctx, pair_result);

        const std::string switch_result = platform::switch_output("bluetooth");
        announce(ctx, switch_result);

        if (ctx.registry != nullptr) {
            ctx.registry->exit();
        }
    }

private:
    std::string selection_buffer_;
    std::vector<platform::BluetoothDevice> scanned_devices_;
};

} // namespace

std::unique_ptr<AppSession> make_bluetooth_setup_app()
{
    return std::make_unique<BluetoothSetupApp>();
}

} // namespace braillatron::ui
