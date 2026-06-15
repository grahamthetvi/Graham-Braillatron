#pragma once

#include "ui_chrome_model.h"

#include <cstddef>
#include <string>
#include <vector>

namespace braillatron::ui {

struct RenderedChrome {
    std::string header;
    std::string breadcrumb;
    std::string weather_line;
    std::vector<std::string> rows;
    size_t focus_row = static_cast<size_t>(-1);
    bool at_top_boundary = false;
    bool at_bottom_boundary = false;
    std::string toast;
    bool tts_paused = false;
    bool dictation_active = false;
};

class ChromeRenderer {
public:
    explicit ChromeRenderer(int max_body_rows = 8);

    RenderedChrome build(const UiChromeModel &model) const;

private:
    int max_body_rows_;
};

} // namespace braillatron::ui
