#include "display_st7789.h"

#include "font8x8_basic.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#ifdef BRAILLATRON_GPIOD
#include <gpiod.h>
#endif

namespace braillatron::ui {

namespace {

constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorWhite = 0xFFFF;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kColorBlue = 0x001F;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

#ifdef BRAILLATRON_GPIOD_V2

void *request_gpio_output(const char *consumer, unsigned int offset,
                          enum gpiod_line_value default_value)
{
    struct gpiod_chip *chip = gpiod_chip_open("/dev/gpiochip0");
    if (chip == nullptr) {
        return nullptr;
    }

    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    if (settings == nullptr) {
        gpiod_chip_close(chip);
        return nullptr;
    }
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_output_value(settings, default_value);

    struct gpiod_line_config *line_cfg = gpiod_line_config_new();
    if (line_cfg == nullptr) {
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        return nullptr;
    }

    const int ret = gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);
    gpiod_line_settings_free(settings);
    if (ret != 0) {
        gpiod_line_config_free(line_cfg);
        gpiod_chip_close(chip);
        return nullptr;
    }

    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    if (req_cfg != nullptr) {
        gpiod_request_config_set_consumer(req_cfg, consumer);
    }

    struct gpiod_line_request *request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);
    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_chip_close(chip);
    return request;
}

void set_gpio_output(void *handle, unsigned int offset, enum gpiod_line_value value)
{
    if (handle == nullptr) {
        return;
    }
    gpiod_line_request_set_value(static_cast<struct gpiod_line_request *>(handle), offset,
                                 value);
}

void release_gpio_handle(void *handle)
{
    if (handle == nullptr) {
        return;
    }
    gpiod_line_request_release(static_cast<struct gpiod_line_request *>(handle));
}

#endif // BRAILLATRON_GPIOD_V2

} // namespace

St7789DisplayBackend::St7789DisplayBackend(const DisplayConfig &config)
    : config_(config)
    , framebuffer_(static_cast<size_t>(config.width) * config.height, kColorBlack)
{
    spi_fd_ = open(config.spidev.c_str(), O_RDWR);
    if (spi_fd_ < 0) {
        return;
    }

    const uint8_t mode = SPI_MODE_0;
    const uint8_t bits = 8;
    const uint32_t speed = 24000000;
    ioctl(spi_fd_, SPI_IOC_WR_MODE, &mode);
    ioctl(spi_fd_, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(spi_fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

#ifdef BRAILLATRON_GPIOD
#if defined(BRAILLATRON_GPIOD_V2)
    if (config.gpio_dc >= 0) {
        gpio_dc_offset_ = config.gpio_dc;
        gpio_dc_handle_ = request_gpio_output("braillatron-dc",
                                              static_cast<unsigned int>(gpio_dc_offset_),
                                              GPIOD_LINE_VALUE_INACTIVE);
        if (gpio_dc_handle_ == nullptr) {
            gpio_dc_offset_ = -1;
        }
    }

    if (config.gpio_rst >= 0) {
        gpio_rst_offset_ = config.gpio_rst;
        gpio_rst_handle_ = request_gpio_output("braillatron-rst",
                                               static_cast<unsigned int>(gpio_rst_offset_),
                                               GPIOD_LINE_VALUE_ACTIVE);
        if (gpio_rst_handle_ != nullptr) {
            set_gpio_output(gpio_rst_handle_, static_cast<unsigned int>(gpio_rst_offset_),
                            GPIOD_LINE_VALUE_INACTIVE);
            usleep(10000);
            set_gpio_output(gpio_rst_handle_, static_cast<unsigned int>(gpio_rst_offset_),
                            GPIOD_LINE_VALUE_ACTIVE);
            usleep(120000);
        } else {
            gpio_rst_offset_ = -1;
        }
    }
#else
    if (config.gpio_dc >= 0) {
        gpio_chip_ = gpiod_chip_open_by_name("gpiochip0");
        if (gpio_chip_ != nullptr) {
            gpio_dc_handle_ =
                gpiod_chip_get_line(static_cast<gpiod_chip *>(gpio_chip_), config.gpio_dc);
            if (gpio_dc_handle_ != nullptr &&
                gpiod_line_request_output(static_cast<gpiod_line *>(gpio_dc_handle_),
                                          "braillatron-dc", 0) == 0) {
                // DC line ready.
            } else {
                gpiod_line_release(static_cast<gpiod_line *>(gpio_dc_handle_));
                gpio_dc_handle_ = nullptr;
            }
        }
    }

    if (config.gpio_rst >= 0 && gpio_chip_ != nullptr) {
        gpio_rst_handle_ =
            gpiod_chip_get_line(static_cast<gpiod_chip *>(gpio_chip_), config.gpio_rst);
        if (gpio_rst_handle_ != nullptr &&
            gpiod_line_request_output(static_cast<gpiod_line *>(gpio_rst_handle_),
                                      "braillatron-rst", 1) == 0) {
            gpiod_line_set_value(static_cast<gpiod_line *>(gpio_rst_handle_), 0);
            usleep(10000);
            gpiod_line_set_value(static_cast<gpiod_line *>(gpio_rst_handle_), 1);
            usleep(120000);
        } else {
            gpiod_line_release(static_cast<gpiod_line *>(gpio_rst_handle_));
            gpio_rst_handle_ = nullptr;
        }
    }
#endif
#else
    (void)config_;
#endif

    ready_ = true;
}

St7789DisplayBackend::~St7789DisplayBackend()
{
    shutdown();
}

bool St7789DisplayBackend::available() const
{
    return ready_ && spi_fd_ >= 0;
}

void St7789DisplayBackend::render(const RenderedChrome &frame)
{
    if (!available()) {
        return;
    }

    std::fill(framebuffer_.begin(), framebuffer_.end(), kColorBlack);

    const auto draw_text = [&](int x, int y, const std::string &text, uint16_t color) {
        int cursor_x = x;
        for (unsigned char ch : text) {
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
                    const int px = cursor_x + col;
                    const int py = y + row;
                    if (px >= 0 && py >= 0 && px < config_.width && py < config_.height) {
                        framebuffer_[static_cast<size_t>(py) * config_.width +
                                       static_cast<size_t>(px)] = color;
                    }
                }
            }
            cursor_x += 9;
            if (cursor_x >= config_.width - 8) {
                break;
            }
        }
    };

    const auto fill_rect = [&](int x, int y, int w, int h, uint16_t color) {
        for (int row = y; row < y + h; ++row) {
            if (row < 0 || row >= config_.height) {
                continue;
            }
            for (int col = x; col < x + w; ++col) {
                if (col < 0 || col >= config_.width) {
                    continue;
                }
                framebuffer_[static_cast<size_t>(row) * config_.width + static_cast<size_t>(col)] =
                    color;
            }
        }
    };

    draw_text(4, 4, frame.header, kColorWhite);
    if (!frame.breadcrumb.empty()) {
        draw_text(4, 16, frame.breadcrumb, kColorYellow);
    }

    int row_y = 32;
    for (size_t i = 0; i < frame.rows.size(); ++i) {
        if (row_y + 10 >= static_cast<int>(config_.height) - 20) {
            break;
        }
        if (i == frame.focus_row) {
            fill_rect(0, row_y - 1, config_.width, 10, kColorBlue);
            draw_text(6, row_y, frame.rows[i], kColorWhite);
        } else {
            draw_text(6, row_y, frame.rows[i], kColorWhite);
        }
        row_y += 12;
    }

    if (!frame.toast.empty()) {
        fill_rect(0, config_.height - 18, config_.width, 18, rgb565(32, 32, 32));
        draw_text(4, config_.height - 16, frame.toast, kColorYellow);
    }

    (void)spi_fd_;
    (void)gpio_dc_handle_;
}

void St7789DisplayBackend::shutdown()
{
#ifdef BRAILLATRON_GPIOD
#if defined(BRAILLATRON_GPIOD_V2)
    release_gpio_handle(gpio_rst_handle_);
    gpio_rst_handle_ = nullptr;
    gpio_rst_offset_ = -1;
    release_gpio_handle(gpio_dc_handle_);
    gpio_dc_handle_ = nullptr;
    gpio_dc_offset_ = -1;
#else
    if (gpio_rst_handle_ != nullptr) {
        gpiod_line_release(static_cast<gpiod_line *>(gpio_rst_handle_));
        gpio_rst_handle_ = nullptr;
    }
    if (gpio_dc_handle_ != nullptr) {
        gpiod_line_release(static_cast<gpiod_line *>(gpio_dc_handle_));
        gpio_dc_handle_ = nullptr;
    }
    if (gpio_chip_ != nullptr) {
        gpiod_chip_close(static_cast<gpiod_chip *>(gpio_chip_));
        gpio_chip_ = nullptr;
    }
#endif
#endif
    if (spi_fd_ >= 0) {
        close(spi_fd_);
        spi_fd_ = -1;
    }
    ready_ = false;
}

} // namespace braillatron::ui
