#pragma once

namespace braillatron::ui {
class OutputHub;
}

namespace braillatron::hooks {

void set_output_hub(ui::OutputHub *hub);

void on_shift_tts_toggle(bool pressed);
void on_speech_ptt_gate(bool open);
void on_menu_overlay(bool open);

} // namespace braillatron::hooks
