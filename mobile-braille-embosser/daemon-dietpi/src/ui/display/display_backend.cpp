#include "display_backend.h"

#include "display_config.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <unistd.h>

#ifdef BRAILLATRON_DISPLAY
#include "display_ncurses.h"
#endif

#include "display_st7789.h"

namespace braillatron::ui {

namespace {

bool path_exists(const std::string &path)
{
    return !path.empty() && access(path.c_str(), F_OK) == 0;
}

bool stdout_is_tty()
{
    return isatty(STDOUT_FILENO) != 0;
}

bool spi_panel_gpio_configured(const DisplayConfig &config)
{
    return config.gpio_dc >= 0;
}

class StubDisplayBackend : public DisplayBackend {
public:
    bool available() const override { return false; }

    void render(const RenderedChrome &frame) override
    {
        std::ostringstream stream;
        stream << frame.header;
        if (!frame.breadcrumb.empty()) {
            stream << " | " << frame.breadcrumb;
        }
        stream << " rows=" << frame.rows.size();
        if (frame.focus_row != static_cast<size_t>(-1) && frame.focus_row < frame.rows.size()) {
            stream << " focus=\"" << frame.rows[frame.focus_row] << "\"";
        }
        if (!frame.toast.empty()) {
            stream << " toast=\"" << frame.toast << "\"";
        }
        std::cerr << "[display] " << stream.str() << "\n";
    }

    void shutdown() override {}
};

DisplayBackend *try_create_spi(const DisplayConfig &config)
{
    auto *backend = new St7789DisplayBackend(config);
    if (!backend->available()) {
        delete backend;
        return nullptr;
    }
    return backend;
}

#ifdef BRAILLATRON_DISPLAY
DisplayBackend *try_create_ncurses(const DisplayConfig &config)
{
    if (!config.ncurses_enabled || !stdout_is_tty()) {
        return nullptr;
    }

    auto *backend = new NcursesDisplayBackend();
    if (!backend->available()) {
        delete backend;
        return nullptr;
    }
    return backend;
}
#endif

} // namespace

DisplayBackend *create_display_backend(const UiConfig &ui_config, const DisplayConfig &display_config)
{
    if (!ui_config.display_enabled) {
        return new StubDisplayBackend();
    }

    const auto pick = [&](DisplayBackendKind kind) -> DisplayBackend * {
        switch (kind) {
        case DisplayBackendKind::Spi:
            return try_create_spi(display_config);
        case DisplayBackendKind::Ncurses:
#ifdef BRAILLATRON_DISPLAY
            return try_create_ncurses(display_config);
#else
            (void)display_config;
            return nullptr;
#endif
        case DisplayBackendKind::Stub:
            return new StubDisplayBackend();
        case DisplayBackendKind::Auto:
            break;
        }
        return nullptr;
    };

    if (display_config.backend != DisplayBackendKind::Auto) {
        DisplayBackend *backend = pick(display_config.backend);
        if (backend != nullptr) {
            return backend;
        }
        return new StubDisplayBackend();
    }

    if (path_exists(display_config.spidev) && spi_panel_gpio_configured(display_config)) {
        DisplayBackend *backend = try_create_spi(display_config);
        if (backend != nullptr) {
            return backend;
        }
    }

#ifdef BRAILLATRON_DISPLAY
    DisplayBackend *ncurses = try_create_ncurses(display_config);
    if (ncurses != nullptr) {
        return ncurses;
    }
#endif

    return new StubDisplayBackend();
}

std::string display_backend_name(const DisplayBackend *backend)
{
    if (backend == nullptr) {
        return "none";
    }
    if (dynamic_cast<const St7789DisplayBackend *>(backend) != nullptr) {
        return "spi";
    }
#ifdef BRAILLATRON_DISPLAY
    if (dynamic_cast<const NcursesDisplayBackend *>(backend) != nullptr) {
        return "ncurses";
    }
#endif
    return "stub";
}

} // namespace braillatron::ui
