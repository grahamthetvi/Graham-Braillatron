#include "global_hooks.h"

#include "../ui/apps/app_registry.h"
#include "../ui/output_hub.h"

namespace braillatron::hooks {

namespace {

ui::OutputHub *g_output_hub = nullptr;
ui::AppRegistry *g_app_registry = nullptr;

} // namespace

void set_output_hub(ui::OutputHub *hub)
{
    g_output_hub = hub;
}

void set_app_registry(ui::AppRegistry *registry)
{
    g_app_registry = registry;
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
    if (g_app_registry != nullptr) {
        g_app_registry->on_global_menu(open);
        return;
    }
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

void on_menu_back()
{
    if (g_output_hub != nullptr) {
        g_output_hub->on_menu_back();
    }
}

void on_safety_broadcast(uint8_t fault_code, uint8_t severity, uint16_t detail)
{
    if (g_output_hub != nullptr) {
        g_output_hub->announce_safety_fault(fault_code, severity, detail);
    }
}

bool standalone_app_active()
{
    return g_app_registry != nullptr && g_app_registry->active() != nullptr;
}

bool inline_app_active()
{
    return g_app_registry != nullptr && g_app_registry->active_inline() != nullptr;
}

std::string active_standalone_app_id()
{
    if (g_app_registry == nullptr || g_app_registry->active() == nullptr) {
        return {};
    }
    return g_app_registry->active()->id();
}

bool app_defers_chord_text()
{
    return g_app_registry != nullptr && g_app_registry->defers_chord_text();
}

void on_app_chord(uint8_t dot_mask)
{
    if (g_app_registry != nullptr) {
        g_app_registry->on_chord(dot_mask);
    }
}

void on_app_text(const std::string &text)
{
    if (g_app_registry != nullptr) {
        g_app_registry->on_text(text);
    }
}

void on_app_control(keyboard::ControlKey key, bool pressed)
{
    if (g_app_registry != nullptr) {
        g_app_registry->on_control(key, pressed);
    }
}

void on_chord_unrecognized(uint8_t dot_mask)
{
    if (dot_mask == 0 || g_output_hub == nullptr) {
        return;
    }
    g_output_hub->announce_message("Unrecognized chord");
    g_output_hub->play_boundary_haptic();
}

} // namespace braillatron::hooks
