#pragma once

#include "accessible_output.h"
#include "../documents/liblouis_bridge.h"
#include "../platform/device_status.h"
#include "backends/backend.h"
#include "display/display_backend.h"
#include "display/display_config.h"
#include "display/remote_frame_publisher.h"
#include "display/ui_chrome_model.h"
#include "menu_overlay.h"
#include "ui_config.h"

#include "../telemetry/telemetry_config.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace braillatron::keyboard {
class FocusNavigator;
}

namespace braillatron::motion {
class MotionService;
}

namespace braillatron::connect {
class ConnectClient;
struct ConnectEvent;
}

namespace braillatron::ui {

class AppRegistry;

class OutputHub : public IAccessibleOutput {
public:
    OutputHub(UiConfig &ui_config, telemetry::TelemetryConfig telemetry_config,
              std::string ui_config_path, DisplayConfig display_config,
              motion::MotionService *motion, documents::BrailleTranslationService *braille,
              documents::BrailleTranslationService *braille_input = nullptr);
    ~OutputHub() override;

    OutputHub(const OutputHub &) = delete;
    OutputHub &operator=(const OutputHub &) = delete;

    void announce_element(const AccessibleElement &element) override;
    void announce_message(const std::string &message) override;
    void stop() override;

    void announce_startup(const platform::DeviceStatusReport &report);
    void announce_focus(const std::string &label, bool at_boundary);
    void announce_spoken(const std::string &message);
    void announce_status_report(const platform::DeviceStatusReport &report);
    void announce_quick_status();

    void on_shift_tts_toggle(bool pressed);
    void on_speech_ptt_gate(bool open);
    void on_menu_overlay(bool open);
    void on_menu_move(bool up);
    void on_menu_activate();
    void on_menu_back();

    void announce_safety_fault(uint8_t fault_code, uint8_t severity, uint16_t detail);
    void check_battery_warning();

    void set_status_report_provider(std::function<void()> provider);
    void set_app_registry(AppRegistry *registry);
    void set_connect_client(connect::ConnectClient *client);
    void set_focus_nav(const keyboard::FocusNavigator *focus_nav);
    void on_connect_event(const connect::ConnectEvent &event);
    void set_stt_transcript_handler(SttBackend::TranscriptHandler handler);
    void set_morse_passive(bool enabled);
    void set_media_playing(bool playing);
    void play_morse(const std::string &text);
    void play_boundary_haptic();
    void request_shutdown();
    void request_restart();
    void open_shutdown_confirm();
    void open_restart_confirm();
    void push_power_confirm(MenuOverlay &mo);

    void sync_chrome(bool at_boundary);
    void tick_display_scroll(uint64_t now_ms);
    void rebuild_display_backend();
    void set_pairing_code_overlay(const std::string &code);
    void clear_pairing_code_overlay();

    MenuOverlay &menu_overlay();
    std::vector<MenuItem> build_settings_menu();
    std::vector<MenuItem> build_app_settings_menu();
    void rebuild_root_menu();

    UiConfig &ui_config() { return ui_config_; }

    void apply_braille_grade_preset(documents::BrailleGradePreset preset);
    void apply_braille_input_preset(documents::BrailleInputPreset preset);
    void show_braille_input_setup_if_needed();

    void release_backends();

private:
    void emit(const std::string &message, bool update_display_toast = true);
    void note_toast_changed(uint64_t now_ms);
    void update_toast_scroll_offset(uint64_t now_ms);
    void persist_ui_config();
    void toggle_bool(bool &field, const char *name);
    void render_chrome();
    void sync_remote_display_publisher();
    void persist_remote_display_config();
    std::vector<MenuItem> build_root_menu();
    std::vector<MenuItem> build_accounts_menu();
    std::vector<MenuItem> build_audio_output_menu();
    std::vector<MenuItem> build_power_confirm_items(std::function<void(MenuOverlay &)> on_cancel);

    UiConfig &ui_config_;
    telemetry::TelemetryConfig telemetry_config_;
    std::string ui_config_path_;
    DisplayConfig display_config_;
    MenuOverlay menu_overlay_;
    AppRegistry *app_registry_ = nullptr;
    connect::ConnectClient *connect_client_ = nullptr;
    const keyboard::FocusNavigator *focus_nav_ = nullptr;
    std::function<void()> status_report_provider_;
    bool morse_passive_ = false;
    bool media_playing_ = false;
    bool media_shift_paused_ = false;
    bool low_battery_announced_ = false;
    bool tts_paused_ = false;
    bool dictation_active_ = false;
    bool signal_link_pending_ = false;
    bool gmail_link_pending_ = false;

    std::vector<MenuItem> build_braille_input_setup_menu();

    motion::MotionService *motion_ = nullptr;
    documents::BrailleTranslationService *braille_service_ = nullptr;
    documents::BrailleTranslationService *braille_input_service_ = nullptr;

    UiChromeModel chrome_model_;

    std::unique_ptr<TtsBackend> tts_;
    std::unique_ptr<BrailleBackend> braille_;
    std::unique_ptr<SttBackend> stt_;
    std::unique_ptr<HapticBackend> haptics_;
    std::unique_ptr<EmbosserBackend> embosser_;
    std::unique_ptr<MorseBackend> morse_;
    std::unique_ptr<DisplayBackend> display_;
    RemoteFramePublisher remote_publisher_;
    std::string remote_display_config_path_;
    bool remote_display_enabled_ = false;
    bool remote_allow_lan_ = false;
    std::string pairing_code_overlay_;
    std::string last_container_;
    std::string last_toast_message_;
};

} // namespace braillatron::ui
