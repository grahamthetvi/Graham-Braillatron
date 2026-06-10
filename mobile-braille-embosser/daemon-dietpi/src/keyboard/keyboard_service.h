#pragma once

#include "chord_engine.h"
#include "evdev_input.h"
#include "evdev_keymap.h"
#include "focus_nav.h"
#include "global_hooks.h"
#include "host_chord_assembler.h"
#include "key_debouncer.h"
#include "keyboard_config.h"
#include "matrix_map.h"
#include "serial_listener.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace braillatron::keyboard {

class KeyboardService {
public:
    explicit KeyboardService(KeyboardConfig config);
    ~KeyboardService();

    KeyboardService(const KeyboardService &) = delete;
    KeyboardService &operator=(const KeyboardService &) = delete;

    void start();
    void stop();
    void poll();

    FocusNavigator &focus_nav();
    bool serial_connected() const;
    bool evdev_connected() const;
    bool try_serial_reconnect();

private:
    void enqueue_frame(const SerialFrame &frame);
    void drain_frame_queue();
    void poll_evdev();
    void handle_key_state(uint16_t key_state);
    void handle_chord(uint8_t dot_mask);
    void handle_safety(const braillatron_safety_broadcast_t &payload);
    void handle_control_edge(const ControlEdge &edge);
    static uint64_t now_ms();

    KeyboardConfig config_;
    MatrixMap matrix_map_;
    SerialListener serial_;
    ChordEngine chord_;
    FocusNavigator focus_;

    std::unique_ptr<EvdevInput> evdev_;
    HostChordAssembler host_chord_assembler_;
    KeyDebouncer evdev_debouncer_;
    EvdevKeymap evdev_keymap_;
    uint16_t evdev_raw_state_ = 0;
    uint16_t evdev_previous_debounced_state_ = 0;

    std::mutex queue_mutex_;
    std::vector<SerialFrame> pending_frames_;

    uint8_t last_announced_fault_ = 0;
    uint8_t last_announced_severity_ = 0;

    std::atomic<bool> running_ {false};
    std::atomic<bool> serial_started_ {false};
    std::atomic<bool> evdev_started_ {false};
};

} // namespace braillatron::keyboard
