#pragma once

#include "../output_hub.h"
#include "ui_context.h"

#include <string>

namespace braillatron::ui {

inline void announce(UiContext &ctx, const std::string &msg)
{
    if (ctx.output != nullptr) {
        ctx.output->announce_message(msg);
    }
}

inline void sync_chrome(UiContext &ctx, bool at_boundary = false)
{
    if (ctx.output != nullptr) {
        ctx.output->sync_chrome(at_boundary);
    }
}

} // namespace braillatron::ui
