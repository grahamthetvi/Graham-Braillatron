#pragma once

#include "app_session.h"
#include "ui_context.h"
#include "word_buffer_input.h"

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
    void poll(uint64_t now_ms);
    void mark_busy(uint64_t now_ms);
    void clear_busy();
    void tick_watchdog(uint64_t now_ms);
    void on_global_menu(bool open);
    void dismiss_frozen_prompt();
    bool frozen_prompt_open() const { return frozen_prompt_open_; }

    void on_chord(uint8_t dot_mask);
    void on_text(const std::string &text);
    void on_control(keyboard::ControlKey key, bool pressed);
    void on_connect_event(const braillatron::connect::ConnectEvent &event);

    std::vector<MenuItem> build_launcher_menu();
    std::vector<MenuItem> build_inline_menu();
    std::vector<std::string> launcher_labels() const;
    std::string launcher_id_for_label(const std::string &label) const;

    bool defers_chord_text() const;
    std::string chord_preview() const;
    void clear_word_buffer();

private:
    AppSession *focused_app() const;
    bool word_buffer_active() const;
    bool word_buffer_uncontracted() const;
    void deliver_text(AppSession *app, const std::string &text);
    void echo_typed_chord(uint8_t dot_mask);
    void echo_typed_text(const std::string &text);
    void echo_typed_commit(const std::string &word);
    void echo_typed_deleted(const std::string &preview_before);

    UiContext ctx_;
    std::vector<std::unique_ptr<AppSession>> apps_;
    AppSession *active_ = nullptr;
    AppSession *active_inline_ = nullptr;
    WordBufferInput word_buffer_;
    uint64_t busy_since_ms_ = 0;
    bool frozen_prompt_open_ = false;

    static constexpr uint64_t kFrozenTimeoutMs = 45000;

    void open_frozen_prompt();
};

} // namespace braillatron::ui
