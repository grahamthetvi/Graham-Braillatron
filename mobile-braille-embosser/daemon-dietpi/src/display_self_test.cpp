#include "ui/display/chrome_renderer.h"
#include "ui/display/ui_chrome_model.h"
#include "ui/menu_overlay.h"

#include <iostream>

namespace {

bool expect_size(size_t actual, size_t expected, const char *label)
{
    if (actual != expected) {
        std::cerr << label << ": expected " << expected << " got " << actual << "\n";
        return false;
    }
    return true;
}

} // namespace

int main()
{
    braillatron::ui::UiChromeModel model;
    model.surface = braillatron::ui::ChromeSurface::Home;
    model.header = "Braillatron";
    model.items = {"Document", "Calculator", "Settings", "Power"};
    model.focus_index = 1;
    model.at_boundary = false;

    braillatron::ui::ChromeRenderer renderer(3);
    const braillatron::ui::RenderedChrome frame = renderer.build(model);
    if (!expect_size(frame.rows.size(), 3, "home scroll rows")) {
        return 1;
    }
    if (frame.focus_row != 1) {
        std::cerr << "home focus row mismatch\n";
        return 1;
    }

    braillatron::ui::MenuItem dynamic_item {
        "TTS",
        []() { return std::string("TTS: On"); },
        {},
    };
    if (braillatron::ui::resolve_menu_item_label(dynamic_item) != "TTS: On") {
        std::cerr << "dynamic menu label mismatch\n";
        return 1;
    }

    model.surface = braillatron::ui::ChromeSurface::InApp;
    model.header = "Calculator";
    model.toast = "Ready";
    model.items.clear();
    const braillatron::ui::RenderedChrome in_app = renderer.build(model);
    if (in_app.rows.size() != 1 || in_app.rows[0] != "Ready") {
        std::cerr << "in-app toast row mismatch\n";
        return 1;
    }

    std::cout << "display self-test ok\n";
    return 0;
}
