#include "ui/display/display_backend.h"
#include "ui/display/display_config.h"
#include "ui/display/chrome_renderer.h"
#include "ui/display/chrome_rasterizer.h"
#include "ui/display/chrome_snapshot.h"
#include "ui/display/ui_chrome_model.h"
#include "ui/ui_config.h"
#include "ui/menu_overlay.h"

#include <iostream>
#include <memory>
#include <vector>

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

    model.composer_line = "hello";
    const braillatron::ui::RenderedChrome with_composer = renderer.build(model);
    if (with_composer.rows.size() != 3 || with_composer.rows[0] != "> hello") {
        std::cerr << "home composer line mismatch\n";
        return 1;
    }
    if (with_composer.focus_row != 2) {
        std::cerr << "home composer focus row mismatch\n";
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

    braillatron::ui::ChromeRasterizer rasterizer;
    braillatron::ui::DisplaySurfaceLayout layout =
        braillatron::ui::layout_for_panel(240, 240);
    std::vector<uint16_t> pixels(static_cast<size_t>(layout.width) * layout.height, 0);
    rasterizer.render(frame, pixels, layout);
    bool has_pixels = false;
    for (uint16_t pixel : pixels) {
        if (pixel != braillatron::ui::ChromeRasterizer::kColorBlack) {
            has_pixels = true;
            break;
        }
    }
    if (!has_pixels) {
        std::cerr << "rasterizer produced blank frame\n";
        return 1;
    }

    const int hdmi_rows = braillatron::ui::max_body_rows_for_layout(
        braillatron::ui::layout_for_hdmi(1920, 1080));
    if (hdmi_rows < 10) {
        std::cerr << "hdmi layout row count too small: " << hdmi_rows << "\n";
        return 1;
    }

    braillatron::ui::UiConfig ui_config;
    ui_config.display_enabled = true;
    braillatron::ui::DisplayConfig display_config;
    display_config.backend = braillatron::ui::DisplayBackendKind::Stub;
    std::unique_ptr<braillatron::ui::DisplayBackend> stub_backend(
        braillatron::ui::create_display_backend(ui_config, display_config));
    if (stub_backend == nullptr) {
        std::cerr << "stub backend creation failed\n";
        return 1;
    }
    if (braillatron::ui::display_backend_name(stub_backend.get()) != "stub") {
        std::cerr << "stub backend label mismatch\n";
        return 1;
    }
    model.surface = braillatron::ui::ChromeSurface::Home;
    model.header = "Braillatron";
    stub_backend->render(model);

    braillatron::ui::RenderedChrome mirror_frame;
    mirror_frame.header = "Mirror";
    mirror_frame.rows = {"one", "two"};
    mirror_frame.focus_row = 1;
    const std::string snapshot =
        braillatron::ui::serialize_chrome_snapshot(mirror_frame, 42);
    braillatron::ui::RenderedChrome parsed;
    uint64_t parsed_seq = 0;
    if (!braillatron::ui::parse_chrome_snapshot(snapshot, parsed, &parsed_seq) ||
        parsed_seq != 42 || parsed.header != "Mirror" || parsed.rows.size() != 2 ||
        parsed.focus_row != 1) {
        std::cerr << "chrome snapshot round-trip mismatch\n";
        return 1;
    }

    display_config.backend = braillatron::ui::DisplayBackendKind::Mirror;
    display_config.mirror_enabled = true;
    display_config.mirror_snapshot = "/tmp/braillatron-display-self-test.snapshot";
    std::unique_ptr<braillatron::ui::DisplayBackend> mirror_backend(
        braillatron::ui::create_display_backend(ui_config, display_config));
    if (mirror_backend == nullptr ||
        braillatron::ui::display_backend_name(mirror_backend.get()) != "mirror") {
        std::cerr << "mirror backend mismatch\n";
        return 1;
    }
    mirror_backend->render(model);

    ui_config.display_enabled = false;
    std::unique_ptr<braillatron::ui::DisplayBackend> disabled_backend(
        braillatron::ui::create_display_backend(ui_config, display_config));
    if (disabled_backend == nullptr ||
        braillatron::ui::display_backend_name(disabled_backend.get()) != "stub") {
        std::cerr << "disabled display backend mismatch\n";
        return 1;
    }

    std::cout << "display self-test ok\n";
    return 0;
}
