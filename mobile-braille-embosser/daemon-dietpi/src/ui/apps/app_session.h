#pragma once

#include "../../keyboard/chord_engine.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace braillatron::connect {
struct ConnectEvent;
}

namespace braillatron::ui {

class LayeredBrowseList;

struct UiContext;

enum class AppKind {
    Standalone,
    Inline,
};

class AppSession {
public:
    virtual ~AppSession() = default;

    virtual std::string id() const = 0;
    virtual std::string label() const = 0;
    virtual AppKind kind() const = 0;
    virtual bool show_in_launcher() const { return kind() == AppKind::Standalone; }

    virtual void on_enter(UiContext &ctx) = 0;
    virtual void on_exit(UiContext &ctx) = 0;
    virtual void on_poll(UiContext &ctx) = 0;
    virtual void on_chord(uint8_t dot_mask, UiContext &ctx) = 0;
    virtual void on_text(const std::string &text, UiContext &ctx) = 0;
    virtual bool buffers_braille_words() const { return false; }
    virtual void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) = 0;
    virtual void on_menu_action(const std::string &action, UiContext &ctx)
    {
        (void)action;
        (void)ctx;
    }
    virtual std::string composer_line() const { return {}; }
    virtual std::string result_line() const { return {}; }
    virtual bool browse_list_active() const { return false; }
    virtual const LayeredBrowseList *browse_list() const { return nullptr; }
    virtual std::vector<std::string> browse_items() const { return {}; }
    virtual size_t browse_focus_index() const { return 0; }
    virtual std::string browse_breadcrumb() const { return {}; }
    virtual void on_connect_event(const braillatron::connect::ConnectEvent &event, UiContext &ctx)
    {
        (void)event;
        (void)ctx;
    }
};

} // namespace braillatron::ui
