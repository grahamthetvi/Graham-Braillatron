#pragma once

#include "../documents/liblouis_bridge.h"
#include "../platform/device_status.h"
#include "backends/backend.h"
#include "display/chrome_renderer.h"
#include "display/display_backend.h"
#include "display/display_config.h"
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
}

namespace braillatron::ui {

class AppRegistry;

class OutputHub {
public:
    OutputHub(UiConfig &ui_config, telemetry::TelemetryConfig telemetry_config,
              std::string ui_config_path, DisplayConfig display_config,
              motion::MotionService *motion, documents::BrailleTranslationService *braille);
    ~OutputHub();

    OutputHub(const OutputHub &) = delete;
    OutputHub &operator=(const OutputHub &) = delete;

    void announce_startup(const platform::DeviceStatusReport &report);
    void announce_focus(const std::string &label, bool at_boundary);
    void announce_message(const std::string &message);
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
    void set_stt_transcript_handler(SttBackend::TranscriptHandler handler);
    void set_morse_passive(bool enabled);
    void set_media_playing(bool playing);
    void play_morse(const std::string &text);
    void play_boundary_haptic();
    void request_shutdown();

    void sync_chrome(bool at_boundary);
    void rebuild_display_backend();

    MenuOverlay &menu_overlay();
    std::vector<MenuItem> build_settings_menu();
    void rebuild_root_menu();

    UiConfig &ui_config() { return ui_config_; }

    void apply_braille_grade_preset(documents::BrailleGradePreset preset);

    void release_backends();

private:
    void emit(const std::string &message, bool update_display_toast = true);
    void persist_ui_config();
    void toggle_bool(bool &field, const char *name);
    void render_chrome();
    std::vector<MenuItem> build_root_menu();
    std::vector<MenuItem> build_accounts_menu();
    std::vector<MenuItem> build_audio_output_menu();

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
    bool low_battery_announced_ = false;
    bool tts_paused_ = false;
    bool dictation_active_ = false;

    motion::MotionService *motion_ = nullptr;
    documents::BrailleTranslationService *braille_service_ = nullptr;

    UiChromeModel chrome_model_;
    ChromeRenderer chrome_renderer_;

    std::unique_ptr<TtsBackend> tts_;
    std::unique_ptr<BrailleBackend> braille_;
    std::unique_ptr<SttBackend> stt_;
    std::unique_ptr<HapticBackend> haptics_;
    std::unique_ptr<EmbosserBackend> embosser_;
    std::unique_ptr<MorseBackend> morse_;
    std::unique_ptr<DisplayBackend> display_;
};

} // namespace braillatron::ui
