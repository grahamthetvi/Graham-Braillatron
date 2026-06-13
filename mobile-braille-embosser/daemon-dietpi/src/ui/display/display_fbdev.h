#pragma once

#include "display_backend.h"
#include "display_config.h"
#include "chrome_rasterizer.h"
#include "chrome_renderer.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {

class FbdevDisplayBackend : public DisplayBackend {
public:
    explicit FbdevDisplayBackend(const DisplayConfig &config);
    ~FbdevDisplayBackend() override;

    FbdevDisplayBackend(const FbdevDisplayBackend &) = delete;
    FbdevDisplayBackend &operator=(const FbdevDisplayBackend &) = delete;

    bool available() const override;
    void render(const UiChromeModel &model) override;
    void shutdown() override;
    std::string backend_label() const override;

private:
    bool map_framebuffer();
    void unmap_framebuffer();
    void blit_to_fb();

    DisplayConfig config_;
    DisplaySurfaceLayout layout_;
    std::unique_ptr<ChromeRenderer> chrome_renderer_;
    ChromeRasterizer rasterizer_;
    int fb_fd_ = -1;
    void *mapped_ = nullptr;
    size_t mapped_size_ = 0;
    uint16_t fb_width_ = 0;
    uint16_t fb_height_ = 0;
    uint32_t fb_bpp_ = 0;
    uint32_t fb_line_length_ = 0;
    bool ready_ = false;
    std::vector<uint16_t> framebuffer_;
};

} // namespace braillatron::ui
