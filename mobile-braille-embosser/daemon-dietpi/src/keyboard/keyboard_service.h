#pragma once

#include "chord_engine.h"
#include "focus_nav.h"
#include "global_hooks.h"
#include "keyboard_config.h"
#include "matrix_map.h"
#include "serial_listener.h"

#include <atomic>
#include <chrono>
#include <cstdint>
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
    bool try_serial_reconnect();

private:
    void enqueue_matrix_state(uint16_t key_state);
    void drain_matrix_queue(uint64_t now_ms);
    void handle_control_edge(const ControlEdge &edge);
    static uint64_t now_ms();

    KeyboardConfig config_;
    MatrixMap matrix_map_;
    SerialListener serial_;
    ChordEngine chord_;
    FocusNavigator focus_;

    std::mutex queue_mutex_;
    std::vector<uint16_t> pending_states_;

    std::atomic<bool> running_ {false};
    std::atomic<bool> serial_started_ {false};
};

} // namespace braillatron::keyboard
