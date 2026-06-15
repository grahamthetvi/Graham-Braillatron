#pragma once

#include "display_backend.h"
#include "display_config.h"

namespace braillatron::ui {

class MirrorDisplayBackend : public DisplayBackend {
public:
    explicit MirrorDisplayBackend(DisplayConfig config);

    bool available() const override;
    void render(const UiChromeModel &model) override;
    void shutdown() override;
    std::string backend_label() const override;

private:
    bool publish_snapshot(const RenderedChrome &frame);

    DisplayConfig config_;
    uint64_t sequence_ = 0;
    bool ready_ = false;
};

MirrorDisplayBackend *try_create_mirror(const DisplayConfig &config);

} // namespace braillatron::ui
