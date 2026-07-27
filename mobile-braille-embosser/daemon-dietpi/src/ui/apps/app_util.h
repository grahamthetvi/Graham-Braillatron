#pragma once

#include "../output_hub.h"
#include "ui_context.h"

#include "../../motion/klipper_motion_bridge.h"
#include "../../motion/motion_service.h"
#include "../../motion_gate.h"

#include <cctype>
#include <string>

namespace braillatron::ui {

inline void announce(UiContext &ctx, const std::string &msg)
{
    if (ctx.output != nullptr) {
        ctx.output->announce_message(msg);
    }
}

/** Speak even while audiobook/music is playing (Backspace leave, critical cues). */
inline void announce_over_media(UiContext &ctx, const std::string &msg)
{
    if (ctx.output != nullptr) {
        ctx.output->announce_over_media(msg);
    }
}

/** Screen-reader label for a typed character (letters, digits, common punctuation). */
inline std::string spoken_typed_char(char ch)
{
    switch (ch) {
    case ' ':
        return "space";
    case '.':
        return "period";
    case ',':
        return "comma";
    case ';':
        return "semicolon";
    case ':':
        return "colon";
    case '!':
        return "exclamation";
    case '?':
        return "question mark";
    case '\'':
        return "apostrophe";
    case '"':
        return "quote";
    case '-':
        return "hyphen";
    case '_':
        return "underscore";
    case '@':
        return "at";
    case '#':
        return "number sign";
    case '*':
        return "asterisk";
    case '/':
        return "slash";
    case '\\':
        return "backslash";
    case '(':
        return "open parenthesis";
    case ')':
        return "close parenthesis";
    case '[':
        return "open bracket";
    case ']':
        return "close bracket";
    case '{':
        return "open brace";
    case '}':
        return "close brace";
    case '+':
        return "plus";
    case '=':
        return "equals";
    case '<':
        return "less than";
    case '>':
        return "greater than";
    case '&':
        return "ampersand";
    case '%':
        return "percent";
    case '$':
        return "dollar";
    case '`':
        return "grave";
    case '~':
        return "tilde";
    case '^':
        return "caret";
    case '|':
        return "pipe";
    case '\n':
        return "new line";
    default:
        break;
    }

    const unsigned char uch = static_cast<unsigned char>(ch);
    if (std::isupper(uch)) {
        return std::string("capital ") + static_cast<char>(std::tolower(uch));
    }
    if (std::isprint(uch)) {
        return std::string(1, ch);
    }
    return "character";
}

inline std::string spoken_typed_text(const std::string &text)
{
    if (text.empty()) {
        return {};
    }
    if (text.size() == 1) {
        return spoken_typed_char(text[0]);
    }
    return text;
}

inline void announce_typing(UiContext &ctx, const std::string &msg)
{
    if (ctx.output != nullptr) {
        ctx.output->announce_typing(msg);
    }
}

inline void sync_chrome(UiContext &ctx, bool at_boundary = false)
{
    if (ctx.output != nullptr) {
        ctx.output->sync_chrome(at_boundary);
    }
}

/** True only when emboss can actually reach motion hardware (not soft-log stubs). */
inline bool emboss_hardware_ready(const UiContext &ctx)
{
    return ctx.motion != nullptr && ctx.braille != nullptr && ctx.klipper != nullptr &&
           ctx.klipper->is_ready() && !MotionGate::is_blocked();
}

/** Persist carriage X + paper Y into the RAM coordinate store (V9 coords.json). */
inline void sync_coords_from_motion(UiContext &ctx, bool save = true)
{
    if (ctx.coords == nullptr || ctx.motion == nullptr) {
        return;
    }
    ctx.coords->mutable_state().x_microsteps =
        ctx.motion->controller().travel_log().position_microsteps();
    ctx.coords->mutable_state().y_line_index = ctx.motion->paper().y_line_index();
    if (save) {
        ctx.coords->save();
    }
}

} // namespace braillatron::ui
