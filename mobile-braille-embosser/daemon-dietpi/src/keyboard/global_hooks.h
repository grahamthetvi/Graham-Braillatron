#pragma once

#include <cstdint>

namespace braillatron::ui {
class OutputHub;
}

namespace braillatron::hooks {

void set_output_hub(ui::OutputHub *hub);

void on_shift_tts_toggle(bool pressed);
void on_speech_ptt_gate(bool open);

bool menu_overlay_open();
void on_menu_overlay(bool open);
void on_menu_move(bool up);
void on_menu_activate();
void on_menu_back();

void on_safety_broadcast(uint8_t fault_code, uint8_t severity, uint16_t detail);

} // namespace braillatron::hooks
