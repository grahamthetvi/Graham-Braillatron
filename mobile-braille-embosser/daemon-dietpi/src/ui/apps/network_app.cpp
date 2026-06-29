#include "app_registry.h"
#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../../platform/network_util.h"
#include "../layered_browse_list.h"
#include "../output_hub.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

constexpr const char *kWifiIface = "wlan0";
constexpr size_t kMaxBrowseLabelLen = 72;

uint64_t steady_now_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::string truncate_for_browse(const std::string &text)
{
    if (text.size() <= kMaxBrowseLabelLen) {
        return text;
    }
    return text.substr(0, kMaxBrowseLabelLen - 3) + "...";
}

std::string wifi_signal_label(const int signal_dbm)
{
    if (signal_dbm >= -55) {
        return "strong";
    }
    if (signal_dbm >= -70) {
        return "good";
    }
    if (signal_dbm >= -80) {
        return "fair";
    }
    return "weak";
}

std::string network_list_label(const platform::WifiNetwork &network)
{
    std::string label = network.ssid;
    label += ", " + wifi_signal_label(network.signal_dbm);
    if (network.secured) {
        label += ", secured";
    }
    return truncate_for_browse(label);
}

enum class Phase {
    Scanning,
    Networks,
    Password,
};

enum class NetworkRowKind {
    Rescan,
    Network,
};

struct NetworkRow {
    NetworkRowKind kind = NetworkRowKind::Network;
    platform::WifiNetwork network;
};

class NetworkApp final : public AppSession {
public:
    std::string id() const override { return "network"; }
    std::string label() const override { return "WiFi"; }
    AppKind kind() const override { return AppKind::Standalone; }
    bool show_in_launcher() const override { return false; }

    bool browse_list_active() const override { return phase_ == Phase::Networks; }

    const LayeredBrowseList *browse_list() const override
    {
        return browse_list_active() ? &browse_ : nullptr;
    }

    std::string composer_line() const override
    {
        return phase_ == Phase::Password ? password_buffer_ : std::string {};
    }

    std::string browse_breadcrumb() const override
    {
        if (phase_ == Phase::Password) {
            return "WiFi > " + pending_ssid_;
        }
        return {};
    }

    bool buffers_braille_words() const override { return phase_ == Phase::Password; }

    void on_enter(UiContext &ctx) override
    {
        reset_session();
        if (!platform::wifi_radio_enabled()) {
            announce(ctx, "WiFi is off. Turn WiFi on in Settings.");
            return;
        }
        start_scan(ctx);
    }

    void on_exit(UiContext &ctx) override
    {
        if (ctx.registry != nullptr) {
            ctx.registry->clear_busy();
        }
        reset_session();
        announce(ctx, "WiFi closed");
    }

    void on_poll(UiContext &ctx) override
    {
        if (!pending_announce_.empty()) {
            announce(ctx, pending_announce_);
            pending_announce_.clear();
        }
        if (phase_ != Phase::Scanning || scan_started_) {
            return;
        }
        scan_started_ = true;
        networks_ = platform::scan_wifi_networks(kWifiIface);
        build_rows();
        phase_ = Phase::Networks;
        sync_browse_list();
        sync_chrome(ctx);
        if (rows_.size() <= 1) {
            pending_announce_ = "No Wi-Fi networks found. Select Rescan.";
        } else {
            pending_announce_ = std::to_string(rows_.size() - 1) + " networks. " +
                                browse_.focused_label();
        }
        if (ctx.registry != nullptr) {
            ctx.registry->clear_busy();
        }
    }

    void on_chord(uint8_t, UiContext &) override {}

    void on_text(const std::string &text, UiContext &ctx) override
    {
        if (phase_ != Phase::Password || text.empty()) {
            return;
        }
        password_buffer_ += text;
        sync_chrome(ctx);
    }

    void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) override
    {
        if (!pressed) {
            return;
        }

        switch (phase_) {
        case Phase::Scanning:
            if (key == keyboard::ControlKey::Backspace && ctx.registry != nullptr) {
                ctx.registry->exit();
            }
            break;
        case Phase::Networks:
            handle_networks_control(key, ctx);
            break;
        case Phase::Password:
            handle_password_control(key, ctx);
            break;
        }
    }

private:
    void reset_session()
    {
        phase_ = Phase::Scanning;
        scan_started_ = false;
        rows_.clear();
        row_index_ = 0;
        pending_ssid_.clear();
        password_buffer_.clear();
        pending_announce_.clear();
        browse_.clear();
    }

    void start_scan(UiContext &ctx)
    {
        phase_ = Phase::Scanning;
        scan_started_ = false;
        rows_.clear();
        browse_.clear();
        pending_announce_ = "Scanning Wi-Fi networks";
        if (ctx.registry != nullptr) {
            ctx.registry->mark_busy(steady_now_ms());
        }
        sync_chrome(ctx);
    }

    void build_rows()
    {
        rows_.clear();
        rows_.push_back(NetworkRow {NetworkRowKind::Rescan, {}});
        for (const auto &network : networks_) {
            rows_.push_back(NetworkRow {NetworkRowKind::Network, network});
        }
        row_index_ = 0;
    }

    void sync_browse_list()
    {
        std::vector<std::string> labels;
        labels.reserve(rows_.size());
        for (const auto &row : rows_) {
            if (row.kind == NetworkRowKind::Rescan) {
                labels.push_back("Rescan");
            } else {
                labels.push_back(network_list_label(row.network));
            }
        }
        browse_.set_items(std::move(labels), row_index_);
        browse_.set_container_name("WiFi");
    }

    void handle_networks_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (ctx.registry != nullptr) {
                ctx.registry->exit();
            }
            return;
        }
        if (key == keyboard::ControlKey::DpadUp) {
            announce_browse_focus(ctx, !browse_.move_up());
            row_index_ = browse_.focus_index();
            return;
        }
        if (key == keyboard::ControlKey::DpadDown) {
            announce_browse_focus(ctx, !browse_.move_down());
            row_index_ = browse_.focus_index();
            return;
        }
        if (key != keyboard::ControlKey::Enter || rows_.empty()) {
            return;
        }

        row_index_ = browse_.focus_index();
        const NetworkRow &row = rows_[row_index_];
        if (row.kind == NetworkRowKind::Rescan) {
            start_scan(ctx);
            return;
        }

        pending_ssid_ = row.network.ssid;
        if (row.network.secured) {
            password_buffer_.clear();
            phase_ = Phase::Password;
            sync_chrome(ctx);
            announce(ctx, "Enter password for " + pending_ssid_ + ". Press Enter to connect.");
            return;
        }

        const std::string result =
            platform::connect_wifi_network(kWifiIface, pending_ssid_, std::string {});
        announce(ctx, result);
    }

    void handle_password_control(keyboard::ControlKey key, UiContext &ctx)
    {
        if (key == keyboard::ControlKey::Backspace) {
            if (!password_buffer_.empty()) {
                password_buffer_.pop_back();
                sync_chrome(ctx);
                return;
            }
            phase_ = Phase::Networks;
            pending_ssid_.clear();
            sync_browse_list();
            sync_chrome(ctx);
            announce_browse_focus(ctx, false);
            return;
        }
        if (key != keyboard::ControlKey::Enter) {
            return;
        }
        if (password_buffer_.empty()) {
            announce(ctx, "Enter a password first");
            return;
        }

        const std::string result =
            platform::connect_wifi_network(kWifiIface, pending_ssid_, password_buffer_);
        announce(ctx, result);
        password_buffer_.clear();
        pending_ssid_.clear();
        phase_ = Phase::Networks;
        sync_browse_list();
        sync_chrome(ctx);
    }

    void announce_browse_focus(UiContext &ctx, bool at_boundary)
    {
        browse_.announce_focus(ctx.output, at_boundary);
    }

    Phase phase_ = Phase::Scanning;
    bool scan_started_ = false;
    std::vector<platform::WifiNetwork> networks_;
    std::vector<NetworkRow> rows_;
    size_t row_index_ = 0;
    std::string pending_ssid_;
    std::string password_buffer_;
    std::string pending_announce_;
    LayeredBrowseList browse_;
};

} // namespace

std::unique_ptr<AppSession> make_network_app()
{
    return std::make_unique<NetworkApp>();
}

} // namespace braillatron::ui
