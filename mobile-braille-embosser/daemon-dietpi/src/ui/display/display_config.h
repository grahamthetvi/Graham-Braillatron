#pragma once

#include <cstdint>
#include <string>

namespace braillatron::ui {

enum class DisplayBackendKind { Auto, Spi, Fb, Ncurses, Stub, Multi };

struct DisplayConfig {
    DisplayBackendKind backend = DisplayBackendKind::Auto;
    std::string spidev = "/dev/spidev3.0";
    std::string fbdev = "/dev/fb0";
    uint16_t width = 240;
    uint16_t height = 240;
    // V5.1 §3.6: header 22/26 are GPIO4 lines 9/7. DietPi RK3566 exposes that
    // bank as gpiochip4; override in display.conf if gpioinfo disagrees.
    std::string gpio_chip = "gpiochip4";
    int gpio_dc = -1;
    int gpio_rst = -1;
    int gpio_cs = -1;
    bool ncurses_enabled = true;
    bool hdmi_enabled = true;
    int hdmi_font_scale = 0;
    bool remote_display_enabled = false;
    std::string remote_display_socket = "/run/braillatron/display.sock";
    std::string remote_display_cmd_socket = "/run/braillatron/display-cmd.sock";
};

DisplayConfig load_display_config(const std::string &path);

} // namespace braillatron::ui
