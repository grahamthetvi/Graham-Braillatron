#pragma once

#include "display_backend.h"

namespace braillatron::ui {

class NcursesDisplayBackend : public DisplayBackend {
public:
    NcursesDisplayBackend();
    ~NcursesDisplayBackend() override;

    NcursesDisplayBackend(const NcursesDisplayBackend &) = delete;
    NcursesDisplayBackend &operator=(const NcursesDisplayBackend &) = delete;

    bool available() const override;
    void render(const UiChromeModel &model) override;
    void shutdown() override;
    std::string backend_label() const override;

private:
    int max_body_rows() const;
    void render_frame(const RenderedChrome &frame);

    bool initialized_ = false;
};

} // namespace braillatron::ui
