#include "keyboard_service.h"

#include "../motion_gate.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace braillatron::keyboard {

namespace {

const char *fault_block_reason(uint8_t fault_code)
{
    switch (fault_code) {
    case BRAILLATRON_FAULT_FREEFALL:
        return "arduino_freefall";
    case BRAILLATRON_FAULT_WATCHDOG_TIMEOUT:
        return "arduino_watchdog_timeout";
    case BRAILLATRON_FAULT_COMMS_LOSS:
        return "arduino_comms_loss";
    case BRAILLATRON_FAULT_BATTERY_CRITICAL:
        return "arduino_battery_critical";
    case BRAILLATRON_FAULT_THERMAL:
        return "arduino_thermal";
    case BRAILLATRON_FAULT_ESTOP:
        return "arduino_estop";
    case BRAILLATRON_FAULT_SENSOR_FAILURE:
        return "arduino_sensor_failure";
    default:
        return "arduino_fault";
    }
}

std::string resolve_config_path(const std::string &path)
{
    if (path.empty() || path[0] == '/') {
        return path;
    }

    const char *env = std::getenv("BRAILLATRON_CONFIG");
    const std::string base = (env != nullptr && env[0] != '\0') ? env : "config";
    return base + "/" + path;
}

} // namespace

KeyboardService::KeyboardService(KeyboardConfig config)
    : config_(std::move(config))
    , matrix_map_(MatrixMap::load(config_.matrix_map_config))
    , serial_(config_.serial_device, config_.baud_rate)
{
    host_chord_assembler_.set_keyboard_matrix_handler(
        [this](uint16_t key_state) { handle_key_state(key_state); });
    host_chord_assembler_.set_chord_handler([this](uint8_t dot_mask) { handle_chord(dot_mask); });
}

KeyboardService::~KeyboardService()
{
    stop();
}

void KeyboardService::start()
{
    if (running_.load()) {
        return;
    }

    running_ = true;

    serial_.set_disconnect_handler([this]() { serial_started_ = false; });

    if (serial_.start([this](const SerialFrame &frame) { enqueue_frame(frame); })) {
        serial_started_ = true;
        std::cerr << "keyboard: listening on " << config_.serial_device << "\n";
    } else {
        serial_started_ = false;
        if (!config_.allow_missing_arduino) {
            throw std::runtime_error("failed to open required serial device " +
                                     config_.serial_device);
        }
        std::cerr << "keyboard: " << config_.serial_device
                  << " unavailable; running without Arduino input\n";
    }

    if (config_.evdev_enabled) {
        const std::string map_path = resolve_config_path(config_.evdev_map_config);
        evdev_keymap_ = EvdevKeymap::load(map_path);

        const auto device_paths = EvdevInput::resolve_device_paths(config_.evdev_device);
        if (device_paths.empty()) {
            std::cerr << "evdev: no suitable input device found\n";
        } else {
            bool any_started = false;
            for (const auto &device_path : device_paths) {
                auto evdev = std::make_unique<EvdevInput>(device_path, config_.evdev_grab);
                if (evdev->start(evdev_keymap_)) {
                    std::cerr << "keyboard: evdev listening on " << device_path
                              << " (bench mode)\n";
                    evdevs_.push_back(std::move(evdev));
                    any_started = true;
                }
            }
            if (any_started) {
                evdev_started_ = true;
                host_chord_assembler_.reset();
                evdev_raw_state_ = 0;
                evdev_previous_debounced_state_ = 0;
                evdev_debouncer_.set_raw_state(0);
            } else {
                evdev_started_ = false;
            }
        }
    }

    if (!serial_started_ && !evdev_started_) {
        if (config_.allow_missing_arduino) {
            std::cerr << "keyboard: no input sources available\n";
            return;
        }
        throw std::runtime_error("no keyboard input sources available");
    }
}

void KeyboardService::stop()
{
    running_ = false;
    serial_.stop();
    serial_started_ = false;

    for (auto &evdev : evdevs_) {
        if (evdev != nullptr) {
            evdev->stop();
        }
    }
    evdevs_.clear();
    evdev_started_ = false;
}

void KeyboardService::poll()
{
    drain_frame_queue();
    poll_evdev();
}

FocusNavigator &KeyboardService::focus_nav()
{
    return focus_;
}

void KeyboardService::set_braille_service(documents::BrailleTranslationService *service)
{
    braille_service_ = service;
}

bool KeyboardService::serial_connected() const
{
    return serial_started_.load() && serial_.is_connected();
}

bool KeyboardService::evdev_connected() const
{
    if (!evdev_started_.load() || evdevs_.empty()) {
        return false;
    }
    for (const auto &evdev : evdevs_) {
        if (evdev != nullptr && evdev->is_connected()) {
            return true;
        }
    }
    return false;
}

bool KeyboardService::try_serial_reconnect()
{
    if (serial_connected()) {
        return true;
    }

    if (serial_.try_reconnect()) {
        serial_started_ = true;
        std::cerr << "keyboard: reconnected to " << config_.serial_device << "\n";
        return true;
    }

    return false;
}

void KeyboardService::enqueue_frame(const SerialFrame &frame)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    pending_frames_.push_back(frame);
}

void KeyboardService::drain_frame_queue()
{
    std::vector<SerialFrame> frames;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        frames.swap(pending_frames_);
    }

    for (const SerialFrame &frame : frames) {
        switch (frame.opcode) {
        case BRAILLATRON_OP_KEYBOARD_MATRIX: {
            braillatron_keyboard_matrix_t payload {};
            std::memcpy(&payload, frame.payload.data(), sizeof(payload));
            handle_key_state(matrix_map_.remap(payload.key_state));
            break;
        }
        case BRAILLATRON_OP_CHORD: {
            braillatron_chord_event_t payload {};
            std::memcpy(&payload, frame.payload.data(), sizeof(payload));
            handle_chord(payload.dot_mask);
            break;
        }
        case BRAILLATRON_OP_SAFETY: {
            braillatron_safety_broadcast_t payload {};
            std::memcpy(&payload, frame.payload.data(), sizeof(payload));
            handle_safety(payload);
            break;
        }
        default:
            break;
        }
    }
}

void KeyboardService::poll_evdev()
{
    if (!evdev_started_ || evdevs_.empty()) {
        return;
    }

    std::vector<EvdevKeyEvent> events;
    for (const auto &evdev : evdevs_) {
        if (evdev != nullptr) {
            evdev->drain_events(events);
        }
    }
    for (const EvdevKeyEvent &event : events) {
        const uint16_t mask = evdev_keymap_.logical_mask_for_code(event.code);
        if (mask == 0) {
            continue;
        }

        if (event.pressed) {
            evdev_raw_state_ = static_cast<uint16_t>(evdev_raw_state_ | mask);
        } else {
            evdev_raw_state_ = static_cast<uint16_t>(evdev_raw_state_ & ~mask);
        }
    }

    evdev_debouncer_.set_raw_state(evdev_raw_state_);

    uint16_t debounced_state = 0;
    const bool debounce_changed = evdev_debouncer_.poll(now_ms(), &debounced_state);
    const bool state_changed =
        debounce_changed && debounced_state != evdev_previous_debounced_state_;

    host_chord_assembler_.update(debounced_state, state_changed, now_ms());
    if (state_changed) {
        evdev_previous_debounced_state_ = debounced_state;
    }
}

void KeyboardService::handle_key_state(uint16_t key_state)
{
    chord_.on_key_state(key_state);

    while (auto edge = chord_.poll_control_edge()) {
        handle_control_edge(*edge);
    }
}

void KeyboardService::handle_chord(uint8_t dot_mask)
{
    std::optional<std::string> text;
    if (braille_service_ != nullptr) {
        text = braille_service_->translate_backward_dots(dot_mask);
    }

    if (hooks::standalone_app_active() || hooks::inline_app_active()) {
        hooks::on_app_chord(dot_mask);
        if (text.has_value()) {
            hooks::on_app_text(*text);
        } else if (dot_mask != 0) {
            hooks::on_chord_unrecognized(dot_mask);
        }
        return;
    }

    if (text.has_value()) {
        focus_.on_text(*text);
    } else if (dot_mask != 0) {
        hooks::on_chord_unrecognized(dot_mask);
    }
}

void KeyboardService::handle_safety(const braillatron_safety_broadcast_t &payload)
{
    if (payload.severity >= BRAILLATRON_SEVERITY_CRITICAL) {
        MotionGate::block(fault_block_reason(payload.fault_code));
    }

    /* Firmware rebroadcasts latched faults; announce each fault only once. */
    if (payload.fault_code == last_announced_fault_ &&
        payload.severity == last_announced_severity_) {
        return;
    }
    last_announced_fault_ = payload.fault_code;
    last_announced_severity_ = payload.severity;

    std::cerr << "keyboard: safety broadcast fault=" << static_cast<unsigned>(payload.fault_code)
              << " severity=" << static_cast<unsigned>(payload.severity)
              << " detail=" << payload.detail << "\n";
    hooks::on_safety_broadcast(payload.fault_code, payload.severity, payload.detail);
}

void KeyboardService::handle_control_edge(const ControlEdge &edge)
{
    const bool menu_open = hooks::menu_overlay_open();

    switch (edge.key) {
    case ControlKey::DpadUp:
        if (edge.pressed) {
            if (menu_open) {
                hooks::on_menu_move(true);
            } else if (hooks::standalone_app_active() || hooks::inline_app_active()) {
                hooks::on_app_control(edge.key, edge.pressed);
            } else {
                focus_.on_dpad_up();
            }
        }
        break;
    case ControlKey::DpadDown:
        if (edge.pressed) {
            if (menu_open) {
                hooks::on_menu_move(false);
            } else if (hooks::standalone_app_active() || hooks::inline_app_active()) {
                hooks::on_app_control(edge.key, edge.pressed);
            } else {
                focus_.on_dpad_down();
            }
        }
        break;
    case ControlKey::Backspace:
        if (edge.pressed) {
            if (menu_open) {
                hooks::on_menu_back();
            } else if (hooks::standalone_app_active() || hooks::inline_app_active()) {
                hooks::on_app_control(edge.key, edge.pressed);
            } else {
                focus_.on_backspace();
            }
        }
        break;
    case ControlKey::Enter:
        if (edge.pressed) {
            if (menu_open) {
                hooks::on_menu_activate();
            } else if (hooks::standalone_app_active() || hooks::inline_app_active()) {
                hooks::on_app_control(edge.key, edge.pressed);
            } else {
                focus_.on_enter();
            }
        }
        break;
    case ControlKey::ShiftTts:
        hooks::on_shift_tts_toggle(edge.pressed);
        break;
    case ControlKey::Speech:
        hooks::on_speech_ptt_gate(edge.pressed);
        break;
    case ControlKey::Menu:
        if (edge.pressed) {
            hooks::on_menu_overlay(!menu_open);
        }
        break;
    }
}

uint64_t KeyboardService::now_ms()
{
    using clock = std::chrono::steady_clock;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch())
            .count());
}

} // namespace braillatron::keyboard
