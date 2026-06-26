#include "chrome_renderer.h"

#include <algorithm>

namespace braillatron::ui {

namespace {

constexpr size_t kNoFocus = static_cast<size_t>(-1);

std::string truncate_line(const std::string &text, size_t max_len)
{
    if (text.size() <= max_len) {
        return text;
    }
    if (max_len <= 3) {
        return text.substr(0, max_len);
    }
    return text.substr(0, max_len - 3) + "...";
}

} // namespace

ChromeRenderer::ChromeRenderer(int max_body_rows)
    : max_body_rows_(max_body_rows > 0 ? max_body_rows : 8)
{
}

RenderedChrome ChromeRenderer::build(const UiChromeModel &model) const
{
    RenderedChrome frame;
    frame.header = model.header;
    frame.breadcrumb = model.breadcrumb;
    frame.weather_line = truncate_line(model.weather_line, 72);
    frame.toast = truncate_line(model.toast, 72);
    frame.tts_paused = model.tts_paused;
    frame.dictation_active = model.dictation_active;
    frame.at_top_boundary = model.at_boundary && model.focus_index == 0;
    frame.at_bottom_boundary =
        model.at_boundary && !model.items.empty() && model.focus_index + 1 >= model.items.size();

    if (model.surface == ChromeSurface::InApp) {
        if (!model.composer_line.empty()) {
            frame.rows.push_back("> " + model.composer_line);
        }
        if (!model.result_line.empty()) {
            frame.rows.push_back("= " + model.result_line);
        }
        if (!frame.toast.empty()) {
            frame.rows.push_back(frame.toast);
        }
        frame.focus_row = kNoFocus;
        return frame;
    }

    if (model.items.empty()) {
        if (model.surface == ChromeSurface::Home && !model.composer_line.empty()) {
            frame.rows.push_back("> " + model.composer_line);
        }
        frame.focus_row = kNoFocus;
        return frame;
    }

    if (model.surface == ChromeSurface::Home && !model.composer_line.empty()) {
        frame.rows.push_back("> " + model.composer_line);
    }

    const size_t focus = std::min(model.focus_index, model.items.size() - 1);
    size_t scroll = 0;
    const int body_rows = model.surface == ChromeSurface::Home && !model.composer_line.empty()
                              ? max_body_rows_ - 1
                              : max_body_rows_;
    if (body_rows <= 0) {
        frame.focus_row = kNoFocus;
        return frame;
    }
    if (focus >= static_cast<size_t>(body_rows)) {
        scroll = focus - static_cast<size_t>(body_rows - 1);
    }

    const size_t end = std::min(model.items.size(), scroll + static_cast<size_t>(body_rows));
    for (size_t i = scroll; i < end; ++i) {
        frame.rows.push_back(model.items[i]);
    }

    if (focus >= scroll && focus < end) {
        const size_t composer_offset =
            (model.surface == ChromeSurface::Home && !model.composer_line.empty()) ? 1 : 0;
        frame.focus_row = focus - scroll + composer_offset;
    } else {
        frame.focus_row = kNoFocus;
    }

    return frame;
}

} // namespace braillatron::ui
