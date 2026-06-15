#pragma once

#include "chrome_renderer.h"

#include <cstdint>
#include <string>

namespace braillatron::ui {

enum class DisplayBackendKind { Auto, Spi, Fb, Ncurses, Stub, Multi, Mirror };

struct DisplayConfig {
    DisplayBackendKind backend = DisplayBackendKind::Auto;
    std::string spidev = "/dev/spidev0.0";
    std::string fbdev = "/dev/fb0";
    uint16_t width = 240;
    uint16_t height = 240;
    int gpio_dc = -1;
    int gpio_rst = -1;
    int gpio_cs = -1;
    bool ncurses_enabled = true;
    bool hdmi_enabled = false;
    int hdmi_font_scale = 0;
    bool mirror_enabled = true;
    std::string mirror_snapshot = "/run/braillatron/ui-chrome.snapshot";
};

DisplayConfig load_display_config(const std::string &path);

} // namespace braillatron::ui
