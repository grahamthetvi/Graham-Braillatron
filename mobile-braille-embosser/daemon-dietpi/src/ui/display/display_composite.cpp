#include "display_composite.h"

#include <sstream>

namespace braillatron::ui {

CompositeDisplayBackend::CompositeDisplayBackend(
    std::vector<std::unique_ptr<DisplayBackend>> backends)
    : backends_(std::move(backends))
{
}

bool CompositeDisplayBackend::available() const
{
    for (const auto &backend : backends_) {
        if (backend != nullptr && backend->available()) {
            return true;
        }
    }
    return false;
}

void CompositeDisplayBackend::render(const UiChromeModel &model)
{
    for (auto &backend : backends_) {
        if (backend != nullptr && backend->available()) {
            backend->render(model);
        }
    }
}

void CompositeDisplayBackend::shutdown()
{
    for (auto &backend : backends_) {
        if (backend != nullptr) {
            backend->shutdown();
        }
    }
    backends_.clear();
}

std::string CompositeDisplayBackend::backend_label() const
{
    std::ostringstream stream;
    bool first = true;
    for (const auto &backend : backends_) {
        if (backend == nullptr || !backend->available()) {
            continue;
        }
        if (!first) {
            stream << '+';
        }
        stream << backend->backend_label();
        first = false;
    }
    if (first) {
        return "composite";
    }
    return stream.str();
}

} // namespace braillatron::ui
