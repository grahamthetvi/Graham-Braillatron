#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../../platform/shell_util.h"

#include <memory>
#include <sstream>
#include <string>

namespace braillatron::ui {
namespace {

class NetworkApp final : public AppSession {
public:
    std::string id() const override { return "network"; }
    std::string label() const override { return "Network and Devices"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        announce(ctx, "Scanning Wi-Fi networks");
        const std::string scan =
            platform::run_command("nmcli -t -f SSID dev wifi list 2>/dev/null");
        std::istringstream stream(scan);
        std::string line;
        int count = 0;
        while (std::getline(stream, line) && count < 10) {
            if (line.empty()) {
                continue;
            }
            announce(ctx, line);
            ++count;
        }
        const std::string bt =
            platform::run_command("nmcli -t -f NAME,TYPE dev status 2>/dev/null | grep bluetooth");
        if (!bt.empty()) {
            announce(ctx, "Bluetooth devices listed in log");
        }
        announce(ctx, "Use QWERTY to enter password after selecting SSID");
    }

    void on_exit(UiContext &ctx) override { announce(ctx, "Network closed"); }
    void on_poll(UiContext &) override {}
    void on_chord(uint8_t, UiContext &) override {}

    void on_text(const std::string &text, UiContext &ctx) override
    {
        if (text.empty()) {
            return;
        }
        if (pending_ssid_.empty()) {
            pending_ssid_ = text;
            announce(ctx, "Selected " + pending_ssid_ + ". Enter password.");
            return;
        }
        pending_password_ += text;
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed || key != keyboard::ControlKey::Enter || pending_ssid_.empty() ||
            pending_password_.empty()) {
            return;
        }
        const std::string cmd = "nmcli dev wifi connect \"" + pending_ssid_ + "\" password \"" +
                                pending_password_ + "\" 2>&1";
        const std::string result = platform::run_command(cmd);
        announce(ctx, result.empty() ? "Connection attempted" : result);
    }

private:
    std::string pending_ssid_;
    std::string pending_password_;
};

} // namespace

std::unique_ptr<AppSession> make_network_app()
{
    return std::make_unique<NetworkApp>();
}

} // namespace braillatron::ui
