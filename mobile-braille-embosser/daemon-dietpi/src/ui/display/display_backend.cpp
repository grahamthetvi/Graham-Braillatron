#include "display_backend.h"

#include "display_config.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <unistd.h>
#include <vector>

#ifdef BRAILLATRON_DISPLAY
#include "display_ncurses.h"
#endif

#include "display_composite.h"
#include "display_fbdev.h"
#include "display_mirror.h"
#include "display_st7789.h"

namespace braillatron::ui {

namespace {

bool path_exists(const std::string &path)
{
    return !path.empty() && access(path.c_str(), F_OK) == 0;
}

bool path_readable(const std::string &path)
{
    return !path.empty() && access(path.c_str(), R_OK) == 0;
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

    void render(const UiChromeModel &model) override
    {
        ChromeRenderer renderer(8);
        const RenderedChrome frame = renderer.build(model);
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

    std::string backend_label() const override { return "stub"; }
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

DisplayBackend *try_create_fb(const DisplayConfig &config)
{
    if (!config.hdmi_enabled) {
        return nullptr;
    }
    auto *backend = new FbdevDisplayBackend(config);
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

void append_mirror_backend(std::vector<std::unique_ptr<DisplayBackend>> &backends,
                           const DisplayConfig &display_config)
{
    if (!display_config.mirror_enabled) {
        return;
    }
    if (DisplayBackend *mirror = try_create_mirror(display_config)) {
        backends.emplace_back(mirror);
    }
}

DisplayBackend *assemble_auto_backends(const DisplayConfig &display_config)
{
    std::vector<std::unique_ptr<DisplayBackend>> pixel_backends;

    if (path_exists(display_config.spidev) && spi_panel_gpio_configured(display_config)) {
        if (DisplayBackend *spi = try_create_spi(display_config)) {
            pixel_backends.emplace_back(spi);
        }
    }

    if (path_readable(display_config.fbdev) && display_config.hdmi_enabled) {
        if (DisplayBackend *fb = try_create_fb(display_config)) {
            pixel_backends.emplace_back(fb);
        }
    }

    std::vector<std::unique_ptr<DisplayBackend>> backends;
    if (pixel_backends.size() == 1) {
        backends.emplace_back(pixel_backends.front().release());
    } else if (pixel_backends.size() > 1) {
        backends.emplace_back(new CompositeDisplayBackend(std::move(pixel_backends)));
    }

    append_mirror_backend(backends, display_config);

    if (!backends.empty()) {
        if (backends.size() == 1) {
            return backends.front().release();
        }
        return new CompositeDisplayBackend(std::move(backends));
    }

#ifdef BRAILLATRON_DISPLAY
    if (DisplayBackend *ncurses = try_create_ncurses(display_config)) {
        return ncurses;
    }
#endif

    if (DisplayBackend *mirror = try_create_mirror(display_config)) {
        return mirror;
    }

    return new StubDisplayBackend();
}

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
        case DisplayBackendKind::Fb:
            return try_create_fb(display_config);
        case DisplayBackendKind::Ncurses:
#ifdef BRAILLATRON_DISPLAY
            return try_create_ncurses(display_config);
#else
            (void)display_config;
            return nullptr;
#endif
        case DisplayBackendKind::Mirror:
            return try_create_mirror(display_config);
        case DisplayBackendKind::Stub:
            return new StubDisplayBackend();
        case DisplayBackendKind::Multi:
        case DisplayBackendKind::Auto:
            break;
        }
        return nullptr;
    };

    if (display_config.backend == DisplayBackendKind::Auto) {
        return assemble_auto_backends(display_config);
    }

    if (display_config.backend == DisplayBackendKind::Multi) {
        return assemble_auto_backends(display_config);
    }

    DisplayBackend *backend = pick(display_config.backend);
    if (backend != nullptr) {
        return backend;
    }
    return new StubDisplayBackend();
}

std::string display_backend_name(const DisplayBackend *backend)
{
    if (backend == nullptr) {
        return "none";
    }
    return backend->backend_label();
}

} // namespace braillatron::ui
