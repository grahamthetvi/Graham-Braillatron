#pragma once

#include "chrome_renderer.h"

#include <cstdint>
#include <vector>

namespace braillatron::ui {

struct DisplaySurfaceLayout {
    uint16_t width = 240;
    uint16_t height = 240;
    int font_scale = 1;
    int margin_left = 4;
    int margin_top = 4;
    int row_stride = 12;
    int toast_band = 18;
};

DisplaySurfaceLayout layout_for_panel(uint16_t width, uint16_t height);
DisplaySurfaceLayout layout_for_hdmi(uint16_t width, uint16_t height);

int max_body_rows_for_layout(const DisplaySurfaceLayout &layout);

class ChromeRasterizer {
public:
    static constexpr uint16_t kColorBlack = 0x0000;
    static constexpr uint16_t kColorWhite = 0xFFFF;
    static constexpr uint16_t kColorYellow = 0xFFE0;
    static constexpr uint16_t kColorBlue = 0x001F;

    static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b);

    void render(const RenderedChrome &frame, std::vector<uint16_t> &buffer,
                const DisplaySurfaceLayout &layout) const;
};

} // namespace braillatron::ui
