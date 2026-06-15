#include "display_fbdev.h"

#include "chrome_frame.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace braillatron::ui {

FbdevDisplayBackend::FbdevDisplayBackend(const DisplayConfig &config)
    : config_(config)
{
    fb_fd_ = open(config.fbdev.c_str(), O_RDWR);
    if (fb_fd_ < 0) {
        return;
    }

    if (!map_framebuffer()) {
        close(fb_fd_);
        fb_fd_ = -1;
        return;
    }

    layout_ = layout_for_hdmi(fb_width_, fb_height_);
    if (config.hdmi_font_scale > 0) {
        layout_.font_scale = config.hdmi_font_scale;
        const int glyph = layout_.font_scale * 8;
        layout_.row_stride = glyph + layout_.font_scale * 4;
        layout_.toast_band = glyph + layout_.font_scale * 8;
    }

    chrome_renderer_ =
        std::make_unique<ChromeRenderer>(max_body_rows_for_layout(layout_));
    framebuffer_.assign(static_cast<size_t>(layout_.width) * layout_.height,
                        ChromeRasterizer::kColorBlack);
    ready_ = true;
}

FbdevDisplayBackend::~FbdevDisplayBackend()
{
    shutdown();
}

bool FbdevDisplayBackend::available() const
{
    return ready_ && fb_fd_ >= 0 && mapped_ != nullptr;
}

std::string FbdevDisplayBackend::backend_label() const
{
    return "fb";
}

bool FbdevDisplayBackend::map_framebuffer()
{
    fb_var_screeninfo var_info {};
    fb_fix_screeninfo fix_info {};

    if (ioctl(fb_fd_, FBIOGET_VSCREENINFO, &var_info) != 0) {
        return false;
    }
    if (ioctl(fb_fd_, FBIOGET_FSCREENINFO, &fix_info) != 0) {
        return false;
    }

    fb_width_ = var_info.xres;
    fb_height_ = var_info.yres;
    fb_bpp_ = var_info.bits_per_pixel;
    fb_line_length_ = fix_info.line_length;

    if (fb_width_ == 0 || fb_height_ == 0 || fb_bpp_ < 16) {
        return false;
    }

    mapped_size_ = fix_info.smem_len;
    if (mapped_size_ == 0) {
        mapped_size_ = static_cast<size_t>(fb_line_length_) * fb_height_;
    }

    mapped_ = mmap(nullptr, mapped_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd_, 0);
    return mapped_ != MAP_FAILED;
}

void FbdevDisplayBackend::unmap_framebuffer()
{
    if (mapped_ != nullptr && mapped_ != MAP_FAILED) {
        munmap(mapped_, mapped_size_);
    }
    mapped_ = nullptr;
    mapped_size_ = 0;
}

void FbdevDisplayBackend::blit_to_fb()
{
    if (!available()) {
        return;
    }

    auto *dst_base = static_cast<uint8_t *>(mapped_);

    for (uint16_t y = 0; y < layout_.height && y < fb_height_; ++y) {
        uint8_t *row = dst_base + static_cast<size_t>(y) * fb_line_length_;
        for (uint16_t x = 0; x < layout_.width && x < fb_width_; ++x) {
            const uint16_t rgb565 =
                framebuffer_[static_cast<size_t>(y) * layout_.width + x];
            const uint8_t r = static_cast<uint8_t>(((rgb565 >> 11) & 0x1F) * 255 / 31);
            const uint8_t g = static_cast<uint8_t>(((rgb565 >> 5) & 0x3F) * 255 / 63);
            const uint8_t b = static_cast<uint8_t>((rgb565 & 0x1F) * 255 / 31);

            if (fb_bpp_ == 32) {
                const size_t offset = static_cast<size_t>(x) * 4;
                row[offset + 0] = b;
                row[offset + 1] = g;
                row[offset + 2] = r;
                row[offset + 3] = 0xFF;
            } else if (fb_bpp_ == 24) {
                const size_t offset = static_cast<size_t>(x) * 3;
                row[offset + 0] = b;
                row[offset + 1] = g;
                row[offset + 2] = r;
            } else if (fb_bpp_ == 16) {
                const size_t offset = static_cast<size_t>(x) * 2;
                row[offset + 0] = static_cast<uint8_t>(rgb565 & 0xFF);
                row[offset + 1] = static_cast<uint8_t>((rgb565 >> 8) & 0xFF);
            }
        }
    }
}

void FbdevDisplayBackend::render(const UiChromeModel &model)
{
    if (!available()) {
        return;
    }

    const ChromeFrame frame =
        rasterize_chrome(model, layout_, *chrome_renderer_, rasterizer_);
    framebuffer_ = frame.pixels;
    blit_to_fb();
}

void FbdevDisplayBackend::shutdown()
{
    unmap_framebuffer();
    if (fb_fd_ >= 0) {
        close(fb_fd_);
        fb_fd_ = -1;
    }
    ready_ = false;
}

} // namespace braillatron::ui
