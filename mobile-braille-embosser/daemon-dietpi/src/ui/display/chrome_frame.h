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

ChromeFrame rasterize_chrome(const UiChromeModel &model, const DisplaySurfaceLayout &layout,
                             ChromeRenderer &renderer, ChromeRasterizer &rasterizer);

} // namespace braillatron::ui
