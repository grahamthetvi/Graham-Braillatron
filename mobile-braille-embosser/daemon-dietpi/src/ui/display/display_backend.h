#pragma once

#include "chrome_renderer.h"
#include "display_config.h"

#include "../ui_config.h"

#include <memory>
#include <string>

namespace braillatron::ui {

class DisplayBackend {
public:
    virtual ~DisplayBackend() = default;

    virtual bool available() const = 0;
    virtual void render(const RenderedChrome &frame) = 0;
    virtual void shutdown() = 0;
};

DisplayBackend *create_display_backend(const UiConfig &ui_config, const DisplayConfig &display_config);
std::string display_backend_name(const DisplayBackend *backend);

} // namespace braillatron::ui
