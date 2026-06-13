#pragma once

#include <cstdint>
#include <string>

namespace braillatron::ui {

enum class DisplayBackendKind { Auto, Spi, Ncurses, Stub };

struct DisplayConfig {
    DisplayBackendKind backend = DisplayBackendKind::Auto;
    std::string spidev = "/dev/spidev0.0";
    uint16_t width = 240;
    uint16_t height = 240;
    int gpio_dc = -1;
    int gpio_rst = -1;
    int gpio_cs = -1;
    bool ncurses_enabled = true;
};

DisplayConfig load_display_config(const std::string &path);

} // namespace braillatron::ui
