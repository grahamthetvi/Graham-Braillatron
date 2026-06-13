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

} // namespace braillatron::ui
