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
    AppSession *active_inline() const { return active_inline_; }

    bool enter(const std::string &id);
    bool enter_inline(const std::string &id);
    void exit_inline();
    void exit();
    bool switch_app(const std::string &id);
    void poll();
    void on_chord(uint8_t dot_mask);
    void on_text(const std::string &text);
    void on_control(keyboard::ControlKey key, bool pressed);
    void on_connect_event(const braillatron::connect::ConnectEvent &event);

    std::vector<MenuItem> build_launcher_menu();
    std::vector<MenuItem> build_inline_menu();
    std::vector<std::string> launcher_labels() const;
    std::string launcher_id_for_label(const std::string &label) const;

private:
    UiContext ctx_;
    std::vector<std::unique_ptr<AppSession>> apps_;
    AppSession *active_ = nullptr;
    AppSession *active_inline_ = nullptr;
};

} // namespace braillatron::ui
