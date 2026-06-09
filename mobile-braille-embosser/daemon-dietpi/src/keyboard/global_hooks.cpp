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

bool menu_overlay_open()
{
    return g_output_hub != nullptr && g_output_hub->menu_overlay().is_open();
}

void on_menu_overlay(bool open)
{
    if (g_output_hub != nullptr) {
        g_output_hub->on_menu_overlay(open);
    }
}

void on_menu_move(bool up)
{
    if (g_output_hub != nullptr) {
        g_output_hub->on_menu_move(up);
    }
}

void on_menu_activate()
{
    if (g_output_hub != nullptr) {
        g_output_hub->on_menu_activate();
    }
}

void on_safety_broadcast(uint8_t fault_code, uint8_t severity, uint16_t detail)
{
    if (g_output_hub != nullptr) {
        g_output_hub->announce_safety_fault(fault_code, severity, detail);
    }
}

} // namespace braillatron::hooks
