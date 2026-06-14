#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../../platform/shell_util.h"

#include <memory>
#include <set>
#include <sstream>
#include <string>

namespace braillatron::ui {
namespace {

constexpr const char *kWifiIface = "wlan0";

std::string shell_quote_wpa(const std::string &value)
{
    return "'\"" + value + "\"'";
}

std::string trim_trailing_ws(const std::string &value)
{
    const auto end = value.find_last_not_of(" \t\r\n");
    if (end == std::string::npos) {
        return {};
    }
    return value.substr(0, end + 1);
}

std::string parse_scan_ssid(const std::string &line)
{
    std::istringstream fields(line);
    std::string field;
    int index = 0;
    while (std::getline(fields, field, '\t')) {
        if (index >= 4) {
            return field;
        }
        ++index;
    }
    return {};
}

class NetworkApp final : public AppSession {
public:
    std::string id() const override { return "network"; }
    std::string label() const override { return "Network and Devices"; }
    AppKind kind() const override { return AppKind::Standalone; }

    void on_enter(UiContext &ctx) override
    {
        announce(ctx, "Scanning Wi-Fi networks");
        const std::string scan = platform::run_command(
            "wpa_cli -i " + std::string(kWifiIface) +
            " scan >/dev/null 2>&1; sleep 2; "
            "wpa_cli -i " +
            std::string(kWifiIface) + " scan_results 2>/dev/null");
        std::istringstream stream(scan);
        std::string line;
        std::set<std::string> seen;
        int count = 0;
        while (std::getline(stream, line) && count < 10) {
            if (line.empty() || line.rfind("bssid", 0) == 0) {
                continue;
            }
            const std::string ssid = parse_scan_ssid(line);
            if (ssid.empty() || seen.count(ssid) != 0) {
                continue;
            }
            seen.insert(ssid);
            announce(ctx, ssid);
            ++count;
        }
        const std::string bt = platform::run_command("bluetoothctl devices 2>/dev/null");
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
        const std::string iface = kWifiIface;
        const std::string id_out =
            platform::run_command("wpa_cli -i " + iface + " add_network 2>/dev/null");
        const std::string net_id = trim_trailing_ws(id_out);
        if (net_id.empty()) {
            announce(ctx, "Failed to add network");
            return;
        }
        const std::string ssid_q = shell_quote_wpa(pending_ssid_);
        const std::string pass_q = shell_quote_wpa(pending_password_);
        platform::run_command("wpa_cli -i " + iface + " set_network " + net_id + " ssid " + ssid_q +
                              " 2>&1");
        platform::run_command("wpa_cli -i " + iface + " set_network " + net_id + " psk " + pass_q +
                              " 2>&1");
        platform::run_command("wpa_cli -i " + iface + " enable_network " + net_id + " 2>&1");
        const std::string result = platform::run_command(
            "wpa_cli -i " + iface + " select_network " + net_id + " 2>&1");
        platform::run_command("wpa_cli -i " + iface + " save_config 2>/dev/null");
        const std::string trimmed = trim_trailing_ws(result);
        announce(ctx, trimmed.empty() ? "Connection attempted" : trimmed);
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
