#pragma once

#include "chrome_renderer.h"

#include <cstdint>
#include <string>

namespace braillatron::ui {

std::string serialize_chrome_snapshot(const RenderedChrome &frame, uint64_t sequence);
bool parse_chrome_snapshot(const std::string &json, RenderedChrome &frame, uint64_t *sequence);

} // namespace braillatron::ui
