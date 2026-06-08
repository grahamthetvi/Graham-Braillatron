#include "global_hooks.h"

#include "../ui/output_hub.h"

namespace braillatron::hooks {

namespace {

ui::OutputHub *g_output_hub = nullptr;

} // namespace

void set_output_hub(ui::OutputHub *hub)
{
    g_output_hub = hub;
}

void on_shift_tts_toggle(bool pressed)
{
    if (g_output_hub != nullptr) {
        g_output_hub->on_shift_tts_toggle(pressed);
    }
}

void on_speech_ptt_gate(bool open)
{
    if (g_output_hub != nullptr) {
        g_output_hub->on_speech_ptt_gate(open);
    }
}

void on_menu_overlay(bool open)
{
    if (g_output_hub != nullptr) {
        g_output_hub->on_menu_overlay(open);
    }
}

} // namespace braillatron::hooks
