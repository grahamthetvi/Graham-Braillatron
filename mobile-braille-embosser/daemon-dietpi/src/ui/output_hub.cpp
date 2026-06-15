#include "output_hub.h"

#include "../documents/liblouis_bridge.h"
#include "../motion/motion_service.h"
#include "../platform/audio_output.h"
#include "../platform/shell_util.h"
#include "../telemetry/system_shutdown.h"
#include "../telemetry/telemetry_bridge.h"
#include "../connect/connect_client.h"
#include "../connect/json_utils.h"
#include "../display/display_client.h"
#include "../display/remote_display_config.h"
#include "apps/app_registry.h"
#include "../keyboard/focus_nav.h"

extern "C" {
#include "protocol.h"
}

#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>

namespace braillatron::ui {

namespace {

constexpr const char *kWeatherCachePath = "/data/braillatron/weather/cache.json";
constexpr uint32_t kWeatherCacheTtlSec = 1800;

std::string load_weather_cache_file()
{
    std::ifstream in(kWeatherCachePath);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

bool weather_cache_is_fresh(const std::string &cache_json)
{
    const std::string fetched_at = connect::json_get_string(cache_json, "fetched_at");
    if (fetched_at.empty()) {
        return false;
    }
    const uint64_t fetched = static_cast<uint64_t>(std::stoull(fetched_at));
    const uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    return now >= fetched && now - fetched <= kWeatherCacheTtlSec;
}

std::string weather_quick_status_line()
{
    const std::string cache = load_weather_cache_file();
    if (cache.empty() || !weather_cache_is_fresh(cache)) {
        return {};
    }

    const size_t current_key = cache.find("\"current\"");
    if (current_key == std::string::npos) {
        return {};
    }
    const size_t current_pos = cache.find('{', current_key);
    if (current_pos == std::string::npos) {
        return {};
    }
    const size_t current_end = cache.find('}', current_pos);
    const std::string current_block = cache.substr(current_pos, current_end - current_pos + 1);
    const std::string temp = connect::json_get_string(current_block, "temperature");
    const std::string description = connect::json_get_string(current_block, "weather_description");
    if (temp.empty() || description.empty()) {
        return {};
    }

    const std::string unit = connect::json_get_string(cache, "temperature_unit");
    std::ostringstream out;
    out << "Weather " << temp;
    if (unit == "fahrenheit") {
        out << " Fahrenheit";
    } else {
        out << " Celsius";
    }
    out << ", " << description << ". ";
    return out.str();
}

std::string weather_chrome_line()
{
    const std::string cache = load_weather_cache_file();
    if (cache.empty() || !weather_cache_is_fresh(cache)) {
        return {};
    }

    const std::string location = connect::json_get_string(cache, "location");
    const size_t current_key = cache.find("\"current\"");
    if (current_key == std::string::npos) {
        return {};
    }
    const size_t current_pos = cache.find('{', current_key);
    if (current_pos == std::string::npos) {
        return {};
    }
    const size_t current_end = cache.find('}', current_pos);
    const std::string current_block = cache.substr(current_pos, current_end - current_pos + 1);
    const std::string temp = connect::json_get_string(current_block, "temperature");
    const std::string description = connect::json_get_string(current_block, "weather_description");
    const std::string humidity = connect::json_get_string(current_block, "relative_humidity");
    const std::string uv = connect::json_get_string(current_block, "uv_index");
    if (temp.empty() || description.empty()) {
        return {};
    }

    const std::string unit = connect::json_get_string(cache, "temperature_unit");
    std::ostringstream out;
    if (!location.empty()) {
        out << location << " ";
    }
    out << temp;
    if (unit == "fahrenheit") {
        out << "F";
    } else {
        out << "C";
    }
    out << " " << description;
    if (!humidity.empty() && humidity != "0") {
        out << " H" << humidity << "%";
    }
    if (!uv.empty() && uv != "0") {
        out << " UV" << uv;
    }
    return out.str();
}

} // namespace

OutputHub::OutputHub(UiConfig &ui_config, telemetry::TelemetryConfig telemetry_config,
                     std::string ui_config_path, DisplayConfig display_config,
                     motion::MotionService *motion, documents::BrailleTranslationService *braille)
    : ui_config_(ui_config)
    , telemetry_config_(std::move(telemetry_config))
    , ui_config_path_(std::move(ui_config_path))
    , display_config_(std::move(display_config))
    , motion_(motion)
    , braille_service_(braille)
    , tts_(create_tts_backend(ui_config_))
    , braille_(create_braille_backend(ui_config_, braille_service_))
    , stt_(create_stt_backend(ui_config_))
    , haptics_(create_haptic_backend(ui_config_, telemetry_config_))
    , embosser_(create_embosser_backend(ui_config_, motion_, braille_service_))
    , morse_(create_morse_backend(ui_config_, telemetry_config_))
    , display_(create_display_backend(ui_config_, display_config_))
    , remote_publisher_(display_config_.remote_display_socket)
    , remote_display_config_path_(braillatron::display::remote_display_config_path_from_env())
{
    const auto remote_config =
        braillatron::display::load_remote_display_config(remote_display_config_path_);
    remote_display_enabled_ = remote_config.enabled || display_config_.remote_display_enabled;
    remote_allow_lan_ = remote_config.allow_lan;
    remote_publisher_.set_enabled(remote_display_enabled_);
    menu_overlay_.set_root_items(build_root_menu());
    if (ui_config_.display_enabled && display_ != nullptr) {
        std::cerr << "[display] backend=" << display_backend_name(display_.get()) << "\n";
    }
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
    if (!label.empty()) {
        emit(label, false);
    }
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

    const std::string ssid = platform::run_command(
        "wpa_cli -i wlan0 status 2>/dev/null | awk -F= "
        "'/^wpa_state=COMPLETED$/ {found=1} found && /^ssid=/ {print substr($0,6); exit}'");
    if (!ssid.empty()) {
        std::string trimmed = ssid;
        const auto end = trimmed.find_last_not_of(" \t\r\n");
        if (end != std::string::npos) {
            trimmed.resize(end + 1);
        }
        stream << "Network " << trimmed << ". ";
    }

    const std::time_t now = std::time(nullptr);
    char time_buf[64] = {0};
    if (std::strftime(time_buf, sizeof(time_buf), "%A %B %d %H:%M", std::localtime(&now)) > 0) {
        stream << "Time " << time_buf << ". ";
    }

    const std::string weather_line = weather_quick_status_line();
    if (!weather_line.empty()) {
        stream << weather_line;
    }

    if (connect_client_ != nullptr) {
        stream << (connect_client_->ping() ? "Connect daemon online. " : "Connect daemon offline. ");
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
        if (pressed) {
            if (!media_shift_paused_) {
                connect_client_->request("media.set_pause", ",\"pause\":true");
                media_shift_paused_ = true;
                emit("Playback paused");
            }
        } else if (media_shift_paused_) {
            connect_client_->request("media.set_pause", ",\"pause\":false");
            media_shift_paused_ = false;
            emit("Playback resumed");
        }
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

void OutputHub::on_connect_event(const connect::ConnectEvent &event)
{
    if (event.type == "message.received") {
        const std::string from = connect::json_get_string(event.data_json, "from");
        const std::string text = connect::json_get_string(event.data_json, "text");
        play_boundary_haptic();
        announce_message("Message from " + from + ". " + text);
        return;
    }

    if (event.type == "signal.link_pending") {
        signal_link_pending_ = true;
        const std::string uri = connect::json_get_string(event.data_json, "uri");
        emit("Open Signal on your phone. Linked Devices. Link New Device.");
        if (!uri.empty() && uri.rfind("sgnl://", 0) == 0) {
            emit(uri);
        }
        return;
    }

    if (event.type == "signal.link_completed") {
        signal_link_pending_ = false;
        emit("Signal linked");
        return;
    }

    if (event.type == "signal.link_failed") {
        signal_link_pending_ = false;
        emit("Signal link not completed. Try again after approving on phone.");
        return;
    }

    if (event.type == "gmail.link_pending") {
        gmail_link_pending_ = true;
        const std::string user_code = connect::json_get_string(event.data_json, "user_code");
        const std::string url = connect::json_get_string(event.data_json, "verification_url");
        emit("Link Gmail at google.com/device");
        if (!user_code.empty()) {
            emit("User code: " + user_code);
        }
        if (!url.empty()) {
            emit(url);
        }
        return;
    }

    if (event.type == "gmail.link_completed") {
        gmail_link_pending_ = false;
        const std::string email = connect::json_get_string(event.data_json, "email");
        emit(email.empty() ? "Gmail linked" : "Gmail linked as " + email);
        return;
    }

    if (event.type == "gmail.link_failed") {
        gmail_link_pending_ = false;
        emit("Gmail link not completed. Try again.");
        return;
    }

    if (event.type == "weather.alert") {
        const std::string message = connect::json_get_string(event.data_json, "message");
        play_boundary_haptic();
        announce_message(message.empty() ? "Weather alert" : "Weather alert. " + message);
        return;
    }

    if (event.type == "weather.updated") {
        chrome_model_.weather_line = weather_chrome_line();
        render_chrome();
    }
}

void OutputHub::sync_chrome(bool at_boundary)
{
    if (!ui_config_.display_enabled || display_ == nullptr) {
        return;
    }

    chrome_model_.at_boundary = at_boundary;
    chrome_model_.tts_paused = tts_paused_;
    chrome_model_.dictation_active = dictation_active_;
    chrome_model_.weather_line = weather_chrome_line();
    chrome_model_.menu_open = menu_overlay_.is_open();
    chrome_model_.menu_depth = menu_overlay_.depth();

    if (menu_overlay_.is_open()) {
        chrome_model_.surface = ChromeSurface::Menu;
        chrome_model_.header = "Menu";
        chrome_model_.composer_line.clear();
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
        chrome_model_.composer_line.clear();
        chrome_model_.items.clear();
        chrome_model_.focus_index = 0;
        chrome_model_.breadcrumb.clear();
    } else if (focus_nav_ != nullptr) {
        chrome_model_.surface = ChromeSurface::Home;
        chrome_model_.header = "Braillatron";
        chrome_model_.composer_line = focus_nav_->input_buffer();
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

    if (!pairing_code_overlay_.empty()) {
        chrome_model_.toast = "Pair code: " + pairing_code_overlay_;
    }

    display_->render(chrome_model_);
    if (remote_display_enabled_) {
        remote_publisher_.publish(chrome_model_);
    }
}

void OutputHub::set_pairing_code_overlay(const std::string &code)
{
    pairing_code_overlay_ = code;
    sync_chrome(false);
}

void OutputHub::clear_pairing_code_overlay()
{
    pairing_code_overlay_.clear();
    sync_chrome(false);
}

void OutputHub::persist_remote_display_config()
{
    braillatron::display::RemoteDisplayConfig config =
        braillatron::display::load_remote_display_config(remote_display_config_path_);
    config.enabled = remote_display_enabled_;
    config.allow_lan = remote_allow_lan_;
    config.frame_socket_path = display_config_.remote_display_socket;
    config.cmd_socket_path = display_config_.remote_display_cmd_socket;
    braillatron::display::save_remote_display_config(remote_display_config_path_, config);
}

void OutputHub::sync_remote_display_publisher()
{
    display_config_.remote_display_enabled = remote_display_enabled_;
    remote_publisher_.set_enabled(remote_display_enabled_);
    persist_remote_display_config();

    braillatron::display::DisplayClient client(display_config_.remote_display_cmd_socket);
    if (remote_display_enabled_) {
        client.request("service.start");
    } else {
        client.request("service.stop");
    }
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
    if (!playing) {
        media_shift_paused_ = false;
    }
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
            "Dictation in Document",
            [this]() {
                return ui_config_.document_dictation_enabled ? "Dictation in Document: On"
                                                             : "Dictation in Document: Off";
            },
            [this](MenuOverlay &mo) {
                (void)mo;
                toggle_bool(ui_config_.document_dictation_enabled, "Dictation in Document");
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
            "Remote display",
            [this]() {
                return remote_display_enabled_ ? "Remote display: On" : "Remote display: Off";
            },
            [this](MenuOverlay &mo) {
                (void)mo;
                remote_display_enabled_ = !remote_display_enabled_;
                sync_remote_display_publisher();
                emit(remote_display_enabled_ ? "Remote display: On" : "Remote display: Off");
            },
        },
        MenuItem {
            "Show pairing code",
            [this]() { return std::string("Show pairing code"); },
            [this](MenuOverlay &mo) {
                (void)mo;
                braillatron::display::DisplayClient client(display_config_.remote_display_cmd_socket);
                const std::string response = client.request("pairing.start");
                const std::string code = connect::json_get_string(response, "code");
                if (code.empty()) {
                    emit("Remote display unavailable.");
                    return;
                }
                set_pairing_code_overlay(code);
                emit("Pairing code " + code + ". Valid for five minutes.");
            },
        },
        MenuItem {
            "Allow LAN access",
            [this]() {
                return remote_allow_lan_ ? "Allow LAN access: On" : "Allow LAN access: Off";
            },
            [this](MenuOverlay &mo) {
                (void)mo;
                remote_allow_lan_ = !remote_allow_lan_;
                persist_remote_display_config();
                braillatron::display::DisplayClient client(display_config_.remote_display_cmd_socket);
                client.request("config.set",
                                 ",\"allow_lan\":" + std::string(remote_allow_lan_ ? "true" : "false"));
                if (remote_allow_lan_) {
                    emit("Warning: LAN access enabled. Anyone on the network can attempt pairing.");
                } else {
                    emit("LAN access disabled. Use SSH tunnel for remote viewing.");
                }
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
                emit(online ? "Connect daemon online" : "Connect daemon offline");
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
                signal_link_pending_ = true;
                connect_client_->request_async("signal.start_link", "", [this](const std::string &response) {
                    if (connect::json_get_bool(response, "linked", false)) {
                        signal_link_pending_ = false;
                        emit("Signal linked");
                        return;
                    }
                    if (connect::json_get_bool(response, "ok", false)) {
                        const std::string uri = connect::json_get_string(response, "uri");
                        if (!uri.empty() && uri.rfind("sgnl://", 0) == 0) {
                            emit(uri);
                        }
                        emit("Approve link on phone. Completion will be announced automatically.");
                    } else {
                        signal_link_pending_ = false;
                        emit("Signal link failed to start");
                    }
                });
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
        MenuItem {
            "Link Gmail",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                if (connect_client_ == nullptr) {
                    emit("Connectivity client unavailable");
                    return;
                }
                gmail_link_pending_ = true;
                connect_client_->request_async("gmail.start_link", "", [this](const std::string &response) {
                    if (connect::json_get_bool(response, "linked", false)) {
                        gmail_link_pending_ = false;
                        const std::string email = connect::json_get_string(response, "email");
                        emit(email.empty() ? "Gmail linked" : "Gmail linked as " + email);
                        return;
                    }
                    if (connect::json_get_bool(response, "ok", false)) {
                        const std::string user_code = connect::json_get_string(response, "user_code");
                        if (!user_code.empty()) {
                            emit("User code: " + user_code);
                        }
                        emit("Visit google.com/device and approve. Completion will be announced.");
                    } else {
                        gmail_link_pending_ = false;
                        emit("Gmail link failed to start");
                    }
                });
            },
        },
        MenuItem {
            "Gmail status",
            {},
            [this](MenuOverlay &mo) {
                (void)mo;
                if (connect_client_ == nullptr) {
                    emit("Connectivity client unavailable");
                    return;
                }
                const std::string status = connect_client_->request("accounts.status");
                const bool linked = connect::json_get_bool(status, "gmail_linked", false);
                emit(linked ? "Gmail linked" : "Gmail not linked");
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
