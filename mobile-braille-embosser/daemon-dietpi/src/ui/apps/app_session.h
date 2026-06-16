#pragma once

#include "../../keyboard/chord_engine.h"

#include <cstdint>
#include <string>

namespace braillatron::connect {
struct ConnectEvent;
}

namespace braillatron::ui {

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

    virtual void on_enter(UiContext &ctx) = 0;
    virtual void on_exit(UiContext &ctx) = 0;
    virtual void on_poll(UiContext &ctx) = 0;
    virtual void on_chord(uint8_t dot_mask, UiContext &ctx) = 0;
    virtual void on_text(const std::string &text, UiContext &ctx) = 0;
    virtual void on_control(keyboard::ControlKey key, bool pressed, UiContext &ctx) = 0;
    virtual std::string composer_line() const { return {}; }
    virtual void on_connect_event(const braillatron::connect::ConnectEvent &event, UiContext &ctx)
    {
        (void)event;
        (void)ctx;
    }
};

} // namespace braillatron::ui
