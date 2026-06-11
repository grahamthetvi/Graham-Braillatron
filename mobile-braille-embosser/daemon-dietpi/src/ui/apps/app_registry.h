#pragma once

#include "app_session.h"
#include "ui_context.h"

#include "../menu_overlay.h"

#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {

class AppRegistry {
public:
    AppRegistry();

    void register_app(std::unique_ptr<AppSession> app);
    void set_context(UiContext ctx);

    const std::vector<std::unique_ptr<AppSession>> &apps() const { return apps_; }
    AppSession *active() const { return active_; }

    bool enter(const std::string &id);
    void exit();
    bool switch_app(const std::string &id);
    void poll();
    void on_chord(uint8_t dot_mask);
    void on_text(const std::string &text);
    void on_control(keyboard::ControlKey key, bool pressed);

    std::vector<MenuItem> build_launcher_menu();
    std::vector<MenuItem> build_inline_menu();

private:
    UiContext ctx_;
    std::vector<std::unique_ptr<AppSession>> apps_;
    AppSession *active_ = nullptr;
};

} // namespace braillatron::ui
