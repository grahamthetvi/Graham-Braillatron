#pragma once

#include "display_backend.h"

#include <memory>
#include <string>
#include <vector>

namespace braillatron::ui {

class CompositeDisplayBackend : public DisplayBackend {
public:
    explicit CompositeDisplayBackend(std::vector<std::unique_ptr<DisplayBackend>> backends);

    bool available() const override;
    void render(const UiChromeModel &model) override;
    void shutdown() override;
    std::string backend_label() const override;

private:
    std::vector<std::unique_ptr<DisplayBackend>> backends_;
};

} // namespace braillatron::ui
