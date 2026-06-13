#include "output_hub.h"

#include "../documents/liblouis_bridge.h"
#include "../motion/motion_service.h"
#include "../platform/audio_output.h"
#include "../telemetry/system_shutdown.h"
#include "../telemetry/telemetry_bridge.h"
#include "../connect/connect_client.h"
#include "../connect/json_utils.h"
#include "apps/app_registry.h"
#include "../keyboard/focus_nav.h"

extern "C" {
#include "protocol.h"
}

#include <chrono>
#include <ctime>
#include <iostream>
#include <sstream>
#include <utility>

namespace braillatron::ui {

OutputHub::OutputHub(UiConfig &ui_config, telemetry::TelemetryConfig telemetry_config,
                     std::string ui_config_path, DisplayConfig display_config,
                     motion::MotionService *motion, documents::BrailleTranslationService *braille)
    : ui_config_(ui_config)
    , telemetry_config_(std::move(telemetry_config))
    , ui_config_path_(std::move(ui_config_path))
    , display_config_(std::move(display_config))
    , motion_(motion)
    , braille_service_(braille)
    , chrome_renderer_(8)
    , tts_(create_tts_backend(ui_config_))
    , braille_(create_braille_backend(ui_config_, braille_service_))
    , stt_(create_stt_backend(ui_config_))
    , haptics_(create_haptic_backend(ui_config_, telemetry_config_))
    , embosser_(create_embosser_backend(ui_config_, motion_, braille_service_))
    , morse_(create_morse_backend(ui_config_, telemetry_config_))
    , display_(create_display_backend(ui_config_, display_config_))
{
    menu_overlay_.set_root_items(build_root_menu());
}

void OutputHub::apply_braille_grade_preset(documents::BrailleGradePreset preset)
{
    if (braille_service_ != nullptr) {
        braille_service_->set_grade_preset(preset);
    }
    ui_config_.braille_table = documents::braille_grade_preset_config_value(preset);
    persist_ui_config();
    emit(std::string("Braille grade: ") + documents::braille_grade_preset_display_label(preset));
}

void OutputHub::release_backends()
{
    if (display_ != nullptr) {
        display_->shutdown();
    }
    display_.reset();
    tts_.reset();
    braille_.reset();
    stt_.reset();
    haptics_.reset();
    embosser_.reset();
    morse_.reset();
}

OutputHub::~OutputHub() = default;

void OutputHub::emit(const std::string &message, bool update_display_toast)
{
    std::cerr << "[ui] " << message << "\n";

    if (media_playing_ && ui_config_.tts_enabled) {
        if (update_display_toast && ui_config_.display_enabled && display_ != nullptr) {
            chrome_model_.toast = message;
            render_chrome();
        }
        return;
    }

    if (ui_config_.tts_enabled && tts_ != nullptr) {
        tts_->speak(message);
    }

    if (ui_config_.braille_enabled && braille_ != nullptr) {
        braille_->write(message);
    }

    const bool emboss_for_deaf_blind =
        ui_config_.deaf_blind_menu_parity && !ui_config_.tts_enabled && ui_config_.embosser_enabled;
    if (emboss_for_deaf_blind && embosser_ != nullptr) {
        embosser_->enqueue_text(message);
    }

    if (morse_passive_ && morse_ != nullptr) {
        morse_->play_text(message);
    }

    if (update_display_toast && ui_config_.display_enabled && display_ != nullptr) {
        chrome_model_.toast = message;
        render_chrome();
    }
}

void OutputHub::announce_startup(const platform::DeviceStatusReport &report)
{
    emit("Braillatron ready");

    const std::vector<std::string> missing = report.missing_user_messages();
    if (missing.empty()) {
        emit("All devices connected");
    } else {
        for (const std::string &message : missing) {
            emit(message);
        }
    }

    if (ui_config_.stt_enabled && stt_ != nullptr) {
        stt_->preload();
    }
}

void OutputHub::announce_focus(const std::string &label, bool at_boundary)
{
    if (label.empty()) {
        return;
    }

    emit(label, false);
    sync_chrome(at_boundary);
    if (at_boundary && ui_config_.haptics_enabled && haptics_ != nullptr) {
        haptics_->play_effect(ui_config_.boundary_haptic_effect);
    }
}

void OutputHub::announce_message(const std::string &message)
{
    emit(message);
}

void OutputHub::announce_status_report(const platform::DeviceStatusReport &report)
{
    std::ostringstream stream;
    stream << "System status. ";
    stream << report.summary_line();
    emit(stream.str());
}

void OutputHub::announce_quick_status()
{
    const telemetry::TelemetrySnapshot snapshot =
        telemetry::read_telemetry_json(telemetry::kTelemetryJsonPath);

    std::ostringstream stream;
    stream << "Quick status. ";
    if (snapshot.battery_percent != BRAILLATRON_TELEMETRY_UNKNOWN) {
        stream << "Battery " << static_cast<unsigned>(snapshot.battery_percent) << " percent. ";
    }

    FILE *wifi = popen("nmcli -t -f ACTIVE,SSID dev wifi 2>/dev/null | grep '^yes' | cut -d: -f2",
                       "r");
    if (wifi != nullptr) {
        char buffer[128] = {0};
        if (fgets(buffer, sizeof(buffer), wifi) != nullptr) {
            stream << "Network " << buffer;
        }
        pclose(wifi);
    }

    const std::time_t now = std::time(nullptr);
    char time_buf[64] = {0};
    if (std::strftime(time_buf, sizeof(time_buf), "%A %B %d %H:%M", std::localtime(&now)) > 0) {
        stream << "Time " << time_buf << ". ";
    }

    emit(stream.str());
}

void OutputHub::check_battery_warning()
{
    if (low_battery_announced_) {
        return;
    }

    const telemetry::TelemetrySnapshot snapshot =
        telemetry::read_telemetry_json(telemetry::kTelemetryJsonPath);
    if (snapshot.battery_percent == BRAILLATRON_TELEMETRY_UNKNOWN ||
        snapshot.battery_percent > 20) {
        return;
    }

    low_battery_announced_ = true;
    emit("Battery low. Twenty percent remaining.");
    play_boundary_haptic();
}

void OutputHub::on_shift_tts_toggle(bool pressed)
{
    if (media_playing_ && connect_client_ != nullptr) {
        connect_client_->request("youtube.pause");
        emit(pressed ? "Playback paused" : "Playback resumed");
        return;
    }

    if (!ui_config_.tts_enabled || tts_ == nullptr) {
        return;
    }

    if (pressed) {
        tts_->pause();
        tts_paused_ = true;
        emit("Speech paused");
    } else {
        tts_->resume();
        tts_paused_ = false;
        emit("Speech resumed");
    }
    sync_chrome(false);
}

void OutputHub::on_speech_ptt_gate(bool open)
{
    if (!ui_config_.stt_enabled) {
        return;
    }

    if (stt_ != nullptr) {
        stt_->set_ptt_open(open);
    }

    if (open) {
        dictation_active_ = true;
        emit("Dictation listening");
    } else {
        dictation_active_ = false;
    }
    sync_chrome(false);
}

void OutputHub::on_menu_overlay(bool open)
{
    if (!open) {
        if (menu_overlay_.is_open()) {
            menu_overlay_.close();
            emit("Menu closed");
            sync_chrome(false);
        }
        return;
    }

    if (app_registry_ != nullptr && app_registry_->active() != nullptr) {
        menu_overlay_.set_root_items(app_registry_->build_inline_menu());
    } else if (app_registry_ != nullptr) {
        menu_overlay_.set_root_items(app_registry_->build_launcher_menu());
    } else {
        menu_overlay_.set_root_items(build_root_menu());
    }

    menu_overlay_.open();
    emit("Menu");
    announce_focus(menu_overlay_.focused_label(), false);
}

void OutputHub::on_menu_move(bool up)
{
    if (!menu_overlay_.is_open()) {
        return;
    }

    const std::string before = menu_overlay_.focused_label();
    if (up) {
        menu_overlay_.move_up();
    } else {
        menu_overlay_.move_down();
    }

    const bool at_boundary = menu_overlay_.focused_label() == before;
    announce_focus(menu_overlay_.focused_label(), at_boundary);
}

void OutputHub::on_menu_activate()
{
    if (!menu_overlay_.is_open()) {
        return;
    }

    const size_t depth_before = menu_overlay_.depth();
    menu_overlay_.activate();
    if (menu_overlay_.depth() > depth_before) {
        announce_focus(menu_overlay_.focused_label(), false);
    }
}

void OutputHub::on_menu_back()
{
    if (!menu_overlay_.is_open()) {
        return;
    }

    if (menu_overlay_.pop_level()) {
        announce_focus(menu_overlay_.focused_label(), false);
        return;
    }

    on_menu_overlay(false);
}

void OutputHub::announce_safety_fault(uint8_t fault_code, uint8_t severity, uint16_t detail)
{
    (void)detail;

    std::string message;
    switch (fault_code) {
    case BRAILLATRON_FAULT_FREEFALL:
        message = "Drop detected. Motors stopped for safety.";
        break;
    case BRAILLATRON_FAULT_WATCHDOG_TIMEOUT:
        message = "Controller watchdog timeout. Motors stopped.";
        break;
    case BRAILLATRON_FAULT_COMMS_LOSS:
        message = "Controller lost contact with the system. Motors stopped.";
        break;
    case BRAILLATRON_FAULT_BATTERY_CRITICAL:
        message = "Battery critically low.";
        break;
    case BRAILLATRON_FAULT_THERMAL:
        message = "Temperature fault. Motors stopped.";
        break;
    case BRAILLATRON_FAULT_ESTOP:
        message = "Emergency stop engaged.";
        break;
    case BRAILLATRON_FAULT_SENSOR_FAILURE:
        message = "Drop sensor unavailable. Motion disabled for safety.";
        break;
    default:
        message = "Safety fault reported by controller.";
        break;
    }

    if (severity >= BRAILLATRON_SEVERITY_CRITICAL) {
        message += " Embossing is paused.";
    }

    emit(message);
    play_boundary_haptic();
}

void OutputHub::set_status_report_provider(std::function<void()> provider)
{
    status_report_provider_ = std::move(provider);
}

void OutputHub::set_app_registry(AppRegistry *registry)
{
    app_registry_ = registry;
    rebuild_root_menu();
}

void OutputHub::set_connect_client(connect::ConnectClient *client)
{
    connect_client_ = client;
}

void OutputHub::set_focus_nav(const keyboard::FocusNavigator *focus_nav)
{
    focus_nav_ = focus_nav;
}

void OutputHub::sync_chrome(bool at_boundary)
{
    if (!ui_config_.display_enabled || display_ == nullptr) {
        return;
    }

    chrome_model_.at_boundary = at_boundary;
    chrome_model_.tts_paused = tts_paused_;
    chrome_model_.dictation_active = dictation_active_;
    chrome_model_.menu_open = menu_overlay_.is_open();
    chrome_model_.menu_depth = menu_overlay_.depth();

    if (menu_overlay_.is_open()) {
        chrome_model_.surface = ChromeSurface::Menu;
        chrome_model_.header = "Menu";
        chrome_model_.items = menu_overlay_.current_item_labels();
        chrome_model_.focus_index = menu_overlay_.focus_index();
        if (menu_overlay_.depth() > 1) {
            chrome_model_.breadcrumb = "Menu > level " + std::to_string(menu_overlay_.depth());
        } else {
            chrome_model_.breadcrumb.clear();
        }
    } else if (app_registry_ != nullptr && app_registry_->active() != nullptr) {
        chrome_model_.surface = ChromeSurface::InApp;
        chrome_model_.header = app_registry_->active()->label();
        chrome_model_.items.clear();
        chrome_model_.focus_index = 0;
        chrome_model_.breadcrumb.clear();
    } else if (focus_nav_ != nullptr) {
        chrome_model_.surface = ChromeSurface::Home;
        chrome_model_.header = "Braillatron";
        chrome_model_.items = focus_nav_->entries();
        chrome_model_.focus_index = focus_nav_->focus_index();
        chrome_model_.breadcrumb.clear();
    }

    render_chrome();
}

void OutputHub::render_chrome()
{
    if (!ui_config_.display_enabled || display_ == nullptr) {
        return;
    }

    const RenderedChrome frame = chrome_renderer_.build(chrome_model_);
    display_->render(frame);
}

void OutputHub::rebuild_display_backend()
{
    if (display_ != nullptr) {
        display_->shutdown();
    }
    display_.reset(create_display_backend(ui_config_, display_config_));
    sync_chrome(false);
}

void OutputHub::set_media_playing(bool playing)
{
    media_playing_ = playing;
}

void OutputHub::set_stt_transcript_handler(SttBackend::TranscriptHandler handler)
{
    if (stt_ != nullptr) {
        stt_->set_transcript_handler(std::move(handler));
    }
}

void OutputHub::set_morse_passive(bool enabled)
{
    morse_passive_ = enabled;
}

void OutputHub::play_morse(const std::string &text)
{
    if (morse_ != nullptr) {
        morse_->play_text(text);
    }
}

void OutputHub::play_boundary_haptic()
{
    if (ui_config_.haptics_enabled && haptics_ != nullptr) {
        haptics_->play_effect(ui_config_.boundary_haptic_effect);
    }
}

void OutputHub::request_shutdown()
{
    emit("Shutting down");
    telemetry::request_clean_shutdown();
}

MenuOverlay &OutputHub::menu_overlay()
{
    return menu_overlay_;
}

void OutputHub::rebuild_root_menu()
{
    if (app_registry_ != nullptr) {
        menu_overlay_.set_root_items(app_registry_->build_launcher_menu());
    } else {
        menu_overlay_.set_root_items(build_root_menu());
    }
}

void OutputHub::persist_ui_config()
{
    if (ui_config_path_.empty()) {
        return;
    }
    save_ui_config(ui_config_path_, ui_config_);
}

void OutputHub::toggle_bool(bool &field, const char *name)
{
    field = !field;
    persist_ui_config();

    if (name == std::string("Speech to Text") && !field && stt_ != nullptr) {
        stt_->set_ptt_open(false);
        dictation_active_ = false;
    }

    if (name == std::string("Visual display")) {
        rebuild_display_backend();
    }

    std::ostringstream stream;
    stream << name << ": " << (field ? "On" : "Off");
    emit(stream.str());
}

std::vector<MenuItem> OutputHub::build_settings_menu()
{
    return {
        MenuItem {
            "Accounts",
            {},
            [this](MenuOverlay &mo) { mo.push_level(build_accounts_menu()); },
        },
        MenuItem {
            "TTS",
            [this]() { return ui_config_.tts_enabled ? "TTS: On" : "TTS: Off"; },
            [this](MenuOverlay &mo) {
                (void)mo;
                toggle_bool(ui_config_.tts_enabled, "TTS");
            },
        },
        MenuItem {
            "Braille",
            [this]() { return ui_config_.braille_enabled ? "Braille: On" : "Braille: Off"; },
            [this](MenuOverlay &mo) {
                (void)mo;
                toggle_bool(ui_config_.braille_enabled, "Braille");
            },
        },
        MenuItem {
            "Embosser",
            [this]() {
                return ui_config_.embosser_enabled ? "Embosser: On" : "Embosser: Off";
            },
            [this](MenuOverlay &mo) {
                (void)mo;
                toggle_bool(ui_config_.embosser_enabled, "Embosser");
            },
        },
        MenuItem {
            "Deaf-blind parity",
            [this]() {
                return ui_config_.deaf_blind_menu_parity ? "Deaf-blind parity: On"
                                                          : "Deaf-blind parity: Off";
            },
            [this](MenuOverlay &mo) {
                (void)mo;
                toggle_bool(ui_config_.deaf_blind_menu_parity, "Deaf-blind parity");
            },
        },
        MenuItem {
            "Speech to Text",
            [this]() {
                return ui_config_.stt_enabled ? "Speech to Text: On" : "Speech to Text: Off";
            },
            [this](MenuOverlay &mo) {
                (void)mo;
                toggle_bool(ui_config_.stt_enabled, "Speech to Text");
            },
        },
        MenuItem {
            "Haptics",
            [this]() { return ui_config_.haptics_enabled ? "Haptics: On" : "Haptics: Off"; },
            [this](MenuOverlay &mo) {
                (void)mo;
                toggle_bool(ui_config_.haptics_enabled, "Haptics");
            },
        },
        MenuItem {
            "Visual display",
            [this]() {
                return ui_config_.display_enabled ? "Visual display: On" : "Visual display: Off";
            },
            [this](MenuOverlay &mo) {
                (void)mo;
                toggle_bool(ui_config_.display_enabled, "Visual display");
            },
        },
        MenuItem {
            "Braille grade",
            [this]() {
                if (braille_service_ != nullptr) {
                    return std::string("Grade: ") + braille_service_->display_label();
                }
                return std::string("Grade: ") + ui_config_.braille_table;
            },
            [this](MenuOverlay &mo) {
                (void)mo;
                if (braille_service_ == nullptr) {
                    return;
                }
                const documents::BrailleGradePreset next =
                    documents::next_braille_grade_preset(braille_service_->current_preset());
                apply_braille_grade_preset(next);
            },
        },
        MenuItem {
            "TTS rate",
            [this]() { return "TTS rate: " + std::to_string(ui_config_.tts_rate); },
            [this](MenuOverlay &mo) {
                (void)mo;
                ui_config_.tts_rate += 10;
                if (ui_config_.tts_rate > 400) {
                    ui_config_.tts_rate = 80;
                }
                persist_ui_config();
                if (tts_ != nullptr) {
                    tts_->set_rate(ui_config_.tts_rate);
                }
                emit("TTS rate: " + std::to_string(ui_config_.tts_rate));
            },
        },
        MenuItem {
            "Audio output",
            [this]() {
                return std::string("Audio: ") +
                       platform::mode_display_label(platform::read_output_mode());
            },
            [this](MenuOverlay &mo) { mo.push_level(build_audio_output_menu()); },
        },
    };
}

std::vector<MenuItem> OutputHub::build_audio_output_menu()
{
    const std::string current_mode = platform::read_output_mode();

    return {
        MenuItem {
            "Aux jack",
            [current_mode]() {
                return std::string("Aux jack") + (current_mode == "aux" ? " (current)" : "");
            },
            [this](MenuOverlay &mo) {
                (void)mo;
                emit(platform::switch_output("aux"));
            },
        },
        MenuItem {
            "Bluetooth speaker",
            [current_mode]() {
                return std::string("Bluetooth speaker") +
                       (current_mode == "bluetooth" ? " (current)" : "");
            },
            [this](MenuOverlay &mo) {
                (void)mo;
                emit(platform::switch_output("bluetooth"));
            },
        },
        MenuItem {
            "I2S amplifier",
            [current_mode]() {
                return std::string("I2S amplifier") + (current_mode == "i2s" ? " (current)" : "");
            },
            [this](MenuOverlay &mo) {
                (void)mo;
                emit(platform::switch_output("i2s"));
            },
        },
        MenuItem {
            "Connect Bluetooth",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                emit(platform::connect_bluetooth());
            },
        },
        MenuItem {
            "Pair Bluetooth speaker",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                menu_overlay_.close();
                if (app_registry_ != nullptr) {
                    app_registry_->enter("bluetooth_setup");
                }
            },
        },
    };
}

std::vector<MenuItem> OutputHub::build_accounts_menu()
{
    return {
        MenuItem {
            "Connectivity status",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                if (connect_client_ == nullptr) {
                    emit("Connectivity client unavailable");
                    return;
                }
                const bool online = connect_client_->ping();
                emit(online ? "Connectd online" : "Connectd offline");
            },
        },
        MenuItem {
            "YouTube cookies",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                if (connect_client_ == nullptr) {
                    emit("Connectivity client unavailable");
                    return;
                }
                const std::string status = connect_client_->request("accounts.status");
                const bool cookies = connect::json_get_bool(status, "youtube_cookies", false);
                emit(cookies ? "YouTube cookies present" : "YouTube cookies missing");
            },
        },
        MenuItem {
            "Import YouTube cookies",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                if (connect_client_ == nullptr) {
                    emit("Connectivity client unavailable");
                    return;
                }
                connect_client_->request("youtube.import_cookies");
                emit("Checking incoming cookie folder");
            },
        },
        MenuItem {
            "Link Signal",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                if (connect_client_ == nullptr) {
                    emit("Connectivity client unavailable");
                    return;
                }
                const std::string start = connect_client_->request("signal.start_link");
                const std::string uri = connect::json_get_string(start, "uri");
                emit("Open Signal on your phone. Linked Devices. Link New Device.");
                if (!uri.empty()) {
                    emit(uri);
                }
                const std::string finish = connect_client_->request("signal.finish_link");
                if (connect::json_get_bool(finish, "ok", false)) {
                    emit("Signal link check complete");
                } else {
                    emit("Signal link pending. Try again after approving on phone.");
                }
            },
        },
        MenuItem {
            "Signal status",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                if (connect_client_ == nullptr) {
                    emit("Connectivity client unavailable");
                    return;
                }
                const std::string status = connect_client_->request("accounts.status");
                const bool linked = connect::json_get_bool(status, "signal_linked", false);
                emit(linked ? "Signal linked" : "Signal not linked");
            },
        },
    };
}

std::vector<MenuItem> OutputHub::build_root_menu()
{
    return {
        MenuItem {
            "Document",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                if (app_registry_ != nullptr) {
                    app_registry_->enter("brailler");
                }
                menu_overlay_.close();
            },
        },
        MenuItem {
            "Settings",
            {},
            [this](MenuOverlay &mo) { mo.push_level(build_settings_menu()); },
        },
        MenuItem {
            "System Status",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                if (status_report_provider_) {
                    status_report_provider_();
                }
            },
        },
        MenuItem {
            "Power",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                request_shutdown();
            },
        },
    };
}

} // namespace braillatron::ui
