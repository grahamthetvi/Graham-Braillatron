#pragma once

#include "display_backend.h"
#include "display_config.h"

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
    void render(const RenderedChrome &frame) override;
    void shutdown() override;

private:
    DisplayConfig config_;
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
