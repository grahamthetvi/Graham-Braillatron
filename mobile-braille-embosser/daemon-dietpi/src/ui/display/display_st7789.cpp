#include "display_st7789.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

#ifdef BRAILLATRON_GPIOD
#include <gpiod.h>
#endif

namespace braillatron::ui {

namespace {

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
    , layout_(layout_for_panel(config.width, config.height))
    , chrome_renderer_(max_body_rows_for_layout(layout_))
    , framebuffer_(static_cast<size_t>(config.width) * config.height, ChromeRasterizer::kColorBlack)
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
#endif

    if (spi_fd_ >= 0 && config.gpio_dc >= 0 && gpio_dc_handle_ != nullptr) {
        ready_ = init_panel();
    }
}

St7789DisplayBackend::~St7789DisplayBackend()
{
    shutdown();
}

bool St7789DisplayBackend::available() const
{
    return ready_ && spi_fd_ >= 0;
}

std::string St7789DisplayBackend::backend_label() const
{
    return "spi";
}

void St7789DisplayBackend::set_dc(bool data_mode)
{
#ifdef BRAILLATRON_GPIOD
#if defined(BRAILLATRON_GPIOD_V2)
    if (gpio_dc_handle_ != nullptr && gpio_dc_offset_ >= 0) {
        set_gpio_output(gpio_dc_handle_, static_cast<unsigned int>(gpio_dc_offset_),
                        data_mode ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
    }
#else
    if (gpio_dc_handle_ != nullptr) {
        gpiod_line_set_value(static_cast<gpiod_line *>(gpio_dc_handle_), data_mode ? 1 : 0);
    }
#endif
#else
    (void)data_mode;
#endif
}

void St7789DisplayBackend::write_command(uint8_t cmd)
{
    if (spi_fd_ < 0) {
        return;
    }
    set_dc(false);
    (void)write(spi_fd_, &cmd, 1);
}

void St7789DisplayBackend::write_data(const uint8_t *data, size_t len)
{
    if (spi_fd_ < 0 || data == nullptr || len == 0) {
        return;
    }
    set_dc(true);
    (void)write(spi_fd_, data, len);
}

bool St7789DisplayBackend::init_panel()
{
    write_command(0x01); // SWRESET
    usleep(150000);

    write_command(0x11); // SLPOUT
    usleep(120000);

    const uint8_t colmod = 0x55; // 16-bit color
    write_command(0x3A);
    write_data(&colmod, 1);

    const uint8_t madctl = 0x00;
    write_command(0x36);
    write_data(&madctl, 1);

    write_command(0x21); // INVON — common for ST7789 modules

    write_command(0x29); // DISPON
    usleep(20000);

    return true;
}

void St7789DisplayBackend::set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    const uint8_t col_data[] = {
        static_cast<uint8_t>((x0 >> 8) & 0xFF),
        static_cast<uint8_t>(x0 & 0xFF),
        static_cast<uint8_t>((x1 >> 8) & 0xFF),
        static_cast<uint8_t>(x1 & 0xFF),
    };
    write_command(0x2A);
    write_data(col_data, sizeof(col_data));

    const uint8_t row_data[] = {
        static_cast<uint8_t>((y0 >> 8) & 0xFF),
        static_cast<uint8_t>(y0 & 0xFF),
        static_cast<uint8_t>((y1 >> 8) & 0xFF),
        static_cast<uint8_t>(y1 & 0xFF),
    };
    write_command(0x2B);
    write_data(row_data, sizeof(row_data));

    write_command(0x2C); // RAMWR
}

void St7789DisplayBackend::flush_framebuffer()
{
    if (!available()) {
        return;
    }

    set_window(0, 0, static_cast<uint16_t>(config_.width - 1),
               static_cast<uint16_t>(config_.height - 1));

    const size_t byte_count = framebuffer_.size() * sizeof(uint16_t);
    const auto *bytes = reinterpret_cast<const uint8_t *>(framebuffer_.data());

    set_dc(true);
    size_t offset = 0;
    while (offset < byte_count) {
        const size_t chunk = std::min(byte_count - offset, static_cast<size_t>(4096));
        const ssize_t written = write(spi_fd_, bytes + offset, chunk);
        if (written <= 0) {
            break;
        }
        offset += static_cast<size_t>(written);
    }
}

void St7789DisplayBackend::render(const UiChromeModel &model)
{
    if (!available()) {
        return;
    }

    const RenderedChrome frame = chrome_renderer_.build(model);
    rasterizer_.render(frame, framebuffer_, layout_);
    flush_framebuffer();
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
