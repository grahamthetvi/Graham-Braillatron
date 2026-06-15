#include "chrome_frame.h"

namespace braillatron::ui {

ChromeFrame rasterize_chrome(const UiChromeModel &model, const DisplaySurfaceLayout &layout)
{
    ChromeFrame out;
    out.layout = layout;
    ChromeRenderer renderer(max_body_rows_for_layout(layout));
    out.text = renderer.build(model);
    out.pixels.assign(static_cast<size_t>(layout.width) * layout.height,
                      ChromeRasterizer::kColorBlack);
    ChromeRasterizer rasterizer;
    rasterizer.render(out.text, out.pixels, layout);
    return out;
}

ChromeFrame rasterize_chrome_panel(const UiChromeModel &model, uint16_t width, uint16_t height)
{
    return rasterize_chrome(model, layout_for_panel(width, height));
}

} // namespace braillatron::ui
