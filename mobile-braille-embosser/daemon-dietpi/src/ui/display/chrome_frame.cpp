#include "chrome_frame.h"

namespace braillatron::ui {

ChromeFrame rasterize_chrome(const UiChromeModel &model, const DisplaySurfaceLayout &layout,
                             ChromeRenderer &renderer, ChromeRasterizer &rasterizer)
{
    ChromeFrame frame;
    frame.layout = layout;
    frame.text = renderer.build(model);
    frame.pixels.assign(static_cast<size_t>(layout.width) * layout.height,
                        ChromeRasterizer::kColorBlack);
    rasterizer.render(frame.text, frame.pixels, layout);
    return frame;
}

} // namespace braillatron::ui
