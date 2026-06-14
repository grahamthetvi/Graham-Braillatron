#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace braillatron::ui {

enum class ChromeSurface { Home, Menu, InApp };

struct UiChromeModel {
    ChromeSurface surface = ChromeSurface::Home;
    std::string header = "Braillatron";
    std::string breadcrumb;
    std::string composer_line;
    std::vector<std::string> items;
    size_t focus_index = 0;
    bool at_boundary = false;
    std::string toast;
    bool menu_open = false;
    size_t menu_depth = 0;
    bool tts_paused = false;
    bool dictation_active = false;
};

std::string resolve_menu_item_label(const struct MenuItem &item);

} // namespace braillatron::ui
