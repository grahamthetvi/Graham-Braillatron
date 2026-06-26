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

constexpr uint64_t kMarqueePauseMs = 1500;
constexpr uint64_t kMarqueeScrollPeriodMs = 60;
constexpr uint64_t kMarqueeEndHoldMs = 2000;

int text_width_pixels(size_t char_count, int char_advance);
int compute_marquee_scroll_offset(int text_width_px, int budget_px, uint64_t epoch_ms,
                                  uint64_t now_ms);
bool marquee_animation_active(int text_width_px, int budget_px, uint64_t epoch_ms,
                              uint64_t now_ms);

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
