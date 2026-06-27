#pragma once

#include "chord_engine.h"

#include <cstdint>
#include <string>

namespace braillatron::ui {
class AppRegistry;
class OutputHub;
}

namespace braillatron::hooks {

void set_output_hub(ui::OutputHub *hub);
void set_app_registry(ui::AppRegistry *registry);

void on_shift_tts_toggle(bool pressed);
void on_speech_ptt_gate(bool open);

bool menu_overlay_open();
void on_menu_overlay(bool open);
void on_menu_move(bool up);
void on_menu_activate();
void on_menu_back();

void on_safety_broadcast(uint8_t fault_code, uint8_t severity, uint16_t detail);

bool standalone_app_active();
bool inline_app_active();
std::string active_standalone_app_id();
bool app_defers_chord_text();
void on_app_chord(uint8_t dot_mask);
void on_app_text(const std::string &text);
void on_app_control(keyboard::ControlKey key, bool pressed);
void on_chord_unrecognized(uint8_t dot_mask);

} // namespace braillatron::hooks
