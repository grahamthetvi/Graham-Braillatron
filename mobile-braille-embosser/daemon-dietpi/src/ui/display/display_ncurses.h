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
    void render(const RenderedChrome &frame) override;
    void shutdown() override;

private:
    bool initialized_ = false;
};

} // namespace braillatron::ui
