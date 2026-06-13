#pragma once

#include "display_backend.h"
#include "display_config.h"
#include "chrome_rasterizer.h"
#include "chrome_renderer.h"

#include <cstdint>
#include <vector>

namespace braillatron::ui {

class St7789DisplayBackend : public DisplayBackend {
public:
    explicit St7789DisplayBackend(const DisplayConfig &config);
    ~St7789DisplayBackend() override;

    St7789DisplayBackend(const St7789DisplayBackend &) = delete;
    St7789DisplayBackend &operator=(const St7789DisplayBackend &) = delete;

    bool available() const override;
    void render(const UiChromeModel &model) override;
    void shutdown() override;
    std::string backend_label() const override;

private:
    void write_command(uint8_t cmd);
    void write_data(const uint8_t *data, size_t len);
    void set_dc(bool data_mode);
    bool init_panel();
    void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    void flush_framebuffer();

    DisplayConfig config_;
    DisplaySurfaceLayout layout_;
    ChromeRenderer chrome_renderer_;
    ChromeRasterizer rasterizer_;
    int spi_fd_ = -1;
    void *gpio_chip_ = nullptr;
    void *gpio_dc_handle_ = nullptr;
    void *gpio_rst_handle_ = nullptr;
    int gpio_dc_offset_ = -1;
    int gpio_rst_offset_ = -1;
    bool ready_ = false;
    std::vector<uint16_t> framebuffer_;
};

} // namespace braillatron::ui
