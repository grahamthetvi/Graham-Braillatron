#include "app_registry.h"
#include "app_session.h"
#include "app_util.h"
#include "ui_context.h"

#include "../../platform/audio_output.h"
#include "../../platform/network_util.h"
#include "../layered_browse_list.h"
#include "../output_hub.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {
namespace {

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

enum class Phase {
    Scanning,
    Devices,
};

class BluetoothSetupApp final : public AppSession {
public:
    std::string id() const override { return "bluetooth_setup"; }
    std::string label() const override { return "Bluetooth"; }
    AppKind kind() const override { return AppKind::Standalone; }
    bool show_in_launcher() const override { return false; }

    bool browse_list_active() const override { return phase_ == Phase::Devices; }

    const LayeredBrowseList *browse_list() const override
    {
        return browse_list_active() ? &browse_ : nullptr;
    }

    std::string browse_breadcrumb() const override { return {}; }

    void on_enter(UiContext &ctx) override
    {
        reset_session();
        if (!platform::bluetooth_powered()) {
            announce(ctx, "Bluetooth is off. Turn Bluetooth on in Settings.");
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
        announce(ctx, "Bluetooth closed");
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
        devices_ = platform::scan_bluetooth_devices();
        build_rows();
        phase_ = Phase::Devices;
        sync_browse_list();
        sync_chrome(ctx);
        if (rows_.size() <= 1) {
            pending_announce_ =
                "No Bluetooth devices found. Put device in pairing mode, then select Rescan.";
        } else {
            pending_announce_ = std::to_string(rows_.size() - 1) + " devices. " +
                                browse_.focused_label();
        }
        if (ctx.registry != nullptr) {
            ctx.registry->clear_busy();
        }
    }

    void on_chord(uint8_t, UiContext &) override {}
    void on_text(const std::string &, UiContext &) override {}
    bool buffers_braille_words() const override { return false; }

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
        case Phase::Devices:
            handle_devices_control(key, ctx);
            break;
        }
    }

private:
    enum class DeviceRowKind {
        Rescan,
        Device,
    };

    struct DeviceRow {
        DeviceRowKind kind = DeviceRowKind::Device;
        platform::BluetoothDevice device;
    };

    void reset_session()
    {
        phase_ = Phase::Scanning;
        scan_started_ = false;
        devices_.clear();
        rows_.clear();
        row_index_ = 0;
        pending_announce_.clear();
        browse_.clear();
    }

    void start_scan(UiContext &ctx)
    {
        phase_ = Phase::Scanning;
        scan_started_ = false;
        devices_.clear();
        rows_.clear();
        browse_.clear();
        pending_announce_ = "Scanning Bluetooth devices";
        if (ctx.registry != nullptr) {
            ctx.registry->mark_busy(steady_now_ms());
        }
        sync_chrome(ctx);
    }

    void build_rows()
    {
        rows_.clear();
        rows_.push_back(DeviceRow {DeviceRowKind::Rescan, {}});
        for (const auto &device : devices_) {
            rows_.push_back(DeviceRow {DeviceRowKind::Device, device});
        }
        row_index_ = 0;
    }

    void sync_browse_list()
    {
        std::vector<std::string> labels;
        labels.reserve(rows_.size());
        for (const auto &row : rows_) {
            if (row.kind == DeviceRowKind::Rescan) {
                labels.push_back("Rescan");
            } else {
                labels.push_back(truncate_for_browse(row.device.name));
            }
        }
        browse_.set_items(std::move(labels), row_index_);
        browse_.set_container_name("Bluetooth");
    }

    void handle_devices_control(keyboard::ControlKey key, UiContext &ctx)
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
        const DeviceRow &row = rows_[row_index_];
        if (row.kind == DeviceRowKind::Rescan) {
            start_scan(ctx);
            return;
        }

        if (!platform::save_bluetooth_mac(row.device.mac)) {
            announce(ctx, "Could not save Bluetooth device address.");
            return;
        }

        announce(ctx, platform::pair_bluetooth_mac(row.device.mac));
        announce(ctx, platform::switch_output("bluetooth"));
    }

    void announce_browse_focus(UiContext &ctx, bool at_boundary)
    {
        browse_.announce_focus(ctx.output, at_boundary);
    }

    Phase phase_ = Phase::Scanning;
    bool scan_started_ = false;
    std::vector<platform::BluetoothDevice> devices_;
    std::vector<DeviceRow> rows_;
    size_t row_index_ = 0;
    std::string pending_announce_;
    LayeredBrowseList browse_;
};

} // namespace

std::unique_ptr<AppSession> make_bluetooth_setup_app()
{
    return std::make_unique<BluetoothSetupApp>();
}

} // namespace braillatron::ui
