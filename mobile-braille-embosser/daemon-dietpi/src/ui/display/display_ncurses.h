#pragma once

#include "display_backend.h"

namespace braillatron::ui {

void render_chrome_terminal(const RenderedChrome &frame, int max_body_rows = 24);

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

    bool initialized_ = false;
};

} // namespace braillatron::ui
