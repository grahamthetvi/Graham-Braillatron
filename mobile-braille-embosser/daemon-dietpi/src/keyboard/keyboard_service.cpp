#include "keyboard_service.h"

#include "../motion_gate.h"

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

} // namespace

KeyboardService::KeyboardService(KeyboardConfig config)
    : config_(std::move(config))
    , matrix_map_(MatrixMap::load(config_.matrix_map_config))
    , serial_(config_.serial_device, config_.baud_rate)
{
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
        return;
    }

    serial_started_ = false;
    if (config_.allow_missing_arduino) {
        std::cerr << "keyboard: " << config_.serial_device
                  << " unavailable; running without physical input\n";
        return;
    }

    throw std::runtime_error("failed to open required serial device " + config_.serial_device);
}

void KeyboardService::stop()
{
    running_ = false;
    serial_.stop();
    serial_started_ = false;
}

void KeyboardService::poll()
{
    drain_frame_queue();
}

FocusNavigator &KeyboardService::focus_nav()
{
    return focus_;
}

bool KeyboardService::serial_connected() const
{
    return serial_started_.load() && serial_.is_connected();
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
            handle_key_state(payload.key_state);
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

void KeyboardService::handle_key_state(uint16_t key_state)
{
    chord_.on_key_state(matrix_map_.remap(key_state));

    while (auto edge = chord_.poll_control_edge()) {
        handle_control_edge(*edge);
    }
}

void KeyboardService::handle_chord(uint8_t dot_mask)
{
    if (auto character = braille_dots_to_char(dot_mask)) {
        focus_.on_character(*character);
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
            } else {
                focus_.on_dpad_up();
            }
        }
        break;
    case ControlKey::DpadDown:
        if (edge.pressed) {
            if (menu_open) {
                hooks::on_menu_move(false);
            } else {
                focus_.on_dpad_down();
            }
        }
        break;
    case ControlKey::Backspace:
        if (edge.pressed) {
            if (menu_open) {
                hooks::on_menu_overlay(false);
            } else {
                focus_.on_backspace();
            }
        }
        break;
    case ControlKey::Enter:
        if (edge.pressed) {
            if (menu_open) {
                hooks::on_menu_activate();
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

} // namespace braillatron::keyboard
