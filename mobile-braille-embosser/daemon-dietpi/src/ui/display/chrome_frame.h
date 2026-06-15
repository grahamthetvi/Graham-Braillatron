#pragma once

#include "chrome_renderer.h"
#include "chrome_rasterizer.h"
#include "ui_chrome_model.h"

#include <cstdint>
#include <vector>

namespace braillatron::ui {

struct ChromeFrame {
    RenderedChrome text;
    std::vector<uint16_t> pixels;
    DisplaySurfaceLayout layout;
};

ChromeFrame rasterize_chrome(const UiChromeModel &model, const DisplaySurfaceLayout &layout);
ChromeFrame rasterize_chrome_panel(const UiChromeModel &model, uint16_t width = 240,
                                   uint16_t height = 240);

} // namespace braillatron::ui
