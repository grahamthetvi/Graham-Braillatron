#include "keyboard_service.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace braillatron::keyboard {

KeyboardService::KeyboardService(KeyboardConfig config)
    : config_(std::move(config))
    , matrix_map_(MatrixMap::load(config_.matrix_map_config))
    , serial_(config_.serial_device, config_.baud_rate)
    , chord_(config_.chord_window_ms)
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

    if (serial_.start([this](uint16_t key_state) { enqueue_matrix_state(key_state); })) {
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
    const uint64_t now = now_ms();
    drain_matrix_queue(now);

    if (auto character = chord_.poll(now)) {
        focus_.on_character(*character);
    }
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

void KeyboardService::enqueue_matrix_state(uint16_t key_state)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    pending_states_.push_back(key_state);
}

void KeyboardService::drain_matrix_queue(uint64_t now_ms)
{
    std::vector<uint16_t> states;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        states.swap(pending_states_);
    }

    for (uint16_t state : states) {
        chord_.on_matrix_state(matrix_map_.remap(state), now_ms);

        while (auto edge = chord_.poll_control_edge()) {
            handle_control_edge(*edge);
        }
    }
}

void KeyboardService::handle_control_edge(const ControlEdge &edge)
{
    switch (edge.key) {
    case ControlKey::DpadUp:
        if (edge.pressed) {
            focus_.on_dpad_up();
        }
        break;
    case ControlKey::DpadDown:
        if (edge.pressed) {
            focus_.on_dpad_down();
        }
        break;
    case ControlKey::Backspace:
        if (edge.pressed) {
            focus_.on_backspace();
        }
        break;
    case ControlKey::Enter:
        if (edge.pressed) {
            focus_.on_enter();
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
            hooks::on_menu_overlay(true);
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
