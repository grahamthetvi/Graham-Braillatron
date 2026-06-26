#include "chrome_rasterizer.h"

#include "font8x8_basic.h"

#include <algorithm>
#include <cmath>

namespace braillatron::ui {

namespace {

int scaled_glyph_size(int font_scale)
{
    return std::max(1, font_scale) * 8;
}

std::string fit_text_to_width(const std::string &text, int max_pixels, int char_advance)
{
    if (max_pixels <= 0 || char_advance <= 0) {
        return {};
    }
    const size_t max_chars = static_cast<size_t>(max_pixels / char_advance);
    if (text.size() <= max_chars) {
        return text;
    }
    if (max_chars <= 3) {
        return text.substr(0, max_chars);
    }
    return text.substr(0, max_chars - 3) + "...";
}

} // namespace

int text_width_pixels(size_t char_count, int char_advance)
{
    if (char_advance <= 0) {
        return 0;
    }
    return static_cast<int>(char_count) * char_advance;
}

int compute_marquee_scroll_offset(int text_width_px, int budget_px, uint64_t epoch_ms,
                                  uint64_t now_ms)
{
    if (text_width_px <= budget_px || budget_px <= 0 || epoch_ms == 0) {
        return 0;
    }

    const int overflow = text_width_px - budget_px;
    const uint64_t elapsed = now_ms >= epoch_ms ? now_ms - epoch_ms : 0;
    if (elapsed < kMarqueePauseMs) {
        return 0;
    }

    const uint64_t scroll_elapsed = elapsed - kMarqueePauseMs;
    const int scroll_px =
        static_cast<int>(std::min<uint64_t>(overflow, scroll_elapsed / kMarqueeScrollPeriodMs));
    return scroll_px;
}

bool marquee_animation_active(int text_width_px, int budget_px, uint64_t epoch_ms, uint64_t now_ms)
{
    if (text_width_px <= budget_px || budget_px <= 0 || epoch_ms == 0) {
        return false;
    }

    const int overflow = text_width_px - budget_px;
    const uint64_t scroll_duration = static_cast<uint64_t>(overflow) * kMarqueeScrollPeriodMs;
    const uint64_t total = kMarqueePauseMs + scroll_duration + kMarqueeEndHoldMs;
    const uint64_t elapsed = now_ms >= epoch_ms ? now_ms - epoch_ms : 0;
    return elapsed < total;
}

namespace {

int body_start_y(const DisplaySurfaceLayout &layout, const RenderedChrome &frame)
{
    const int glyph = scaled_glyph_size(layout.font_scale);
    const int header_y = layout.margin_top;
    int y = header_y + glyph + layout.font_scale * 2;
    if (!frame.breadcrumb.empty()) {
        y += glyph + layout.font_scale * 2;
    }
    if (!frame.weather_line.empty()) {
        y += glyph + layout.font_scale * 2;
    }
    return y + layout.font_scale * 2;
}

} // namespace

DisplaySurfaceLayout layout_for_panel(uint16_t width, uint16_t height)
{
    DisplaySurfaceLayout layout;
    layout.width = width;
    layout.height = height;
    layout.font_scale = 1;
    layout.margin_left = 4;
    layout.margin_top = 4;
    layout.row_stride = 12;
    layout.toast_band = 18;
    return layout;
}

DisplaySurfaceLayout layout_for_hdmi(uint16_t width, uint16_t height)
{
    DisplaySurfaceLayout layout;
    layout.width = width;
    layout.height = height;

    const int min_dim = static_cast<int>(std::min(width, height));
    if (min_dim >= 1080) {
        layout.font_scale = 4;
    } else if (min_dim >= 720) {
        layout.font_scale = 3;
    } else if (min_dim >= 480) {
        layout.font_scale = 2;
    } else {
        layout.font_scale = 1;
    }

    layout.margin_left = 24;
    layout.margin_top = 24;
    const int glyph = scaled_glyph_size(layout.font_scale);
    layout.row_stride = glyph + layout.font_scale * 4;
    layout.toast_band = glyph + layout.font_scale * 8;
    return layout;
}

int max_body_rows_for_layout(const DisplaySurfaceLayout &layout)
{
    const RenderedChrome empty_frame;
    const int start = body_start_y(layout, empty_frame);
    const int available = static_cast<int>(layout.height) - start - layout.toast_band;
    if (available <= 0 || layout.row_stride <= 0) {
        return 1;
    }
    return std::max(1, available / layout.row_stride);
}

uint16_t ChromeRasterizer::rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void ChromeRasterizer::render(const RenderedChrome &frame, std::vector<uint16_t> &buffer,
                              const DisplaySurfaceLayout &layout) const
{
    const size_t pixel_count = static_cast<size_t>(layout.width) * layout.height;
    if (buffer.size() != pixel_count) {
        buffer.assign(pixel_count, kColorBlack);
    } else {
        std::fill(buffer.begin(), buffer.end(), kColorBlack);
    }

    const int glyph = scaled_glyph_size(layout.font_scale);
    const int char_advance = glyph + layout.font_scale;

    const auto put_pixel = [&](int x, int y, uint16_t color) {
        if (x < 0 || y < 0 || x >= layout.width || y >= layout.height) {
            return;
        }
        buffer[static_cast<size_t>(y) * layout.width + static_cast<size_t>(x)] = color;
    };

    const auto fill_rect = [&](int x, int y, int w, int h, uint16_t color) {
        for (int row = y; row < y + h; ++row) {
            for (int col = x; col < x + w; ++col) {
                put_pixel(col, row, color);
            }
        }
    };

    const int text_budget = static_cast<int>(layout.width) - layout.margin_left - glyph;

    const auto draw_text = [&](int x, int y, const std::string &text, uint16_t color,
                               int scroll_offset_px = 0, bool allow_ellipsis = true) {
        const int clip_left = layout.margin_left;
        const int clip_right = static_cast<int>(layout.width) - layout.font_scale;
        std::string draw_text_value = text;
        if (allow_ellipsis && scroll_offset_px == 0) {
            draw_text_value =
                fit_text_to_width(text, text_budget - (x - layout.margin_left), char_advance);
        }
        int cursor_x = x - scroll_offset_px;
        for (unsigned char ch : draw_text_value) {
            if (ch < 32 || ch > 126) {
                ch = '?';
            }
            const size_t idx = static_cast<size_t>(ch - 32) * 8;
            for (int row = 0; row < 8; ++row) {
                const uint8_t bits = kFont8x8Basic[idx + static_cast<size_t>(row)];
                for (int col = 0; col < 8; ++col) {
                    if ((bits & (1 << col)) == 0) {
                        continue;
                    }
                    for (int sy = 0; sy < layout.font_scale; ++sy) {
                        for (int sx = 0; sx < layout.font_scale; ++sx) {
                            const int px = cursor_x + col * layout.font_scale + sx;
                            const int py = y + row * layout.font_scale + sy;
                            if (px < clip_left || px >= clip_right) {
                                continue;
                            }
                            put_pixel(px, py, color);
                        }
                    }
                }
            }
            cursor_x += char_advance;
            if (cursor_x >= clip_right + char_advance) {
                break;
            }
        }
    };

    const int header_y = layout.margin_top;
    const int breadcrumb_y = header_y + glyph + layout.font_scale * 2;

    draw_text(layout.margin_left, header_y, frame.header, kColorWhite);
    if (!frame.breadcrumb.empty()) {
        draw_text(layout.margin_left, breadcrumb_y, frame.breadcrumb, kColorYellow);
    }
    int weather_y = breadcrumb_y;
    if (!frame.breadcrumb.empty()) {
        weather_y += glyph + layout.font_scale * 2;
    }
    if (!frame.weather_line.empty()) {
        draw_text(layout.margin_left, weather_y, frame.weather_line, kColorYellow);
    }

    int row_y = body_start_y(layout, frame);
    const int row_band = glyph + layout.font_scale * 2;
    for (size_t i = 0; i < frame.rows.size(); ++i) {
        if (row_y + row_band >= static_cast<int>(layout.height) - layout.toast_band) {
            break;
        }
        if (i == frame.focus_row) {
            fill_rect(0, row_y - layout.font_scale, layout.width, row_band, kColorBlue);
            draw_text(layout.margin_left + layout.font_scale * 2, row_y, frame.rows[i],
                      kColorWhite);
        } else {
            draw_text(layout.margin_left + layout.font_scale * 2, row_y, frame.rows[i],
                      kColorWhite);
        }
        row_y += layout.row_stride;
    }

    if (!frame.toast.empty()) {
        fill_rect(0, static_cast<int>(layout.height) - layout.toast_band, layout.width,
                  layout.toast_band, rgb565(32, 32, 32));
        draw_text(layout.margin_left, static_cast<int>(layout.height) - layout.toast_band +
                                           layout.font_scale * 2, frame.toast, kColorYellow,
                      frame.toast_scroll_offset_px, false);
    }
}

} // namespace braillatron::ui
