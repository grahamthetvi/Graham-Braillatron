#include "menu_overlay.h"

#include <utility>

namespace braillatron::ui {

MenuOverlay::MenuOverlay(std::vector<std::string> entries)
    : entries_(std::move(entries))
{
}

bool MenuOverlay::is_open() const
{
    return open_;
}

void MenuOverlay::open()
{
    open_ = true;
    focus_index_ = 0;
}

void MenuOverlay::close()
{
    open_ = false;
}

void MenuOverlay::move_up()
{
    if (!open_ || entries_.empty() || focus_index_ == 0) {
        return;
    }
    --focus_index_;
}

void MenuOverlay::move_down()
{
    if (!open_ || entries_.empty() || focus_index_ + 1 >= entries_.size()) {
        return;
    }
    ++focus_index_;
}

void MenuOverlay::activate()
{
    if (!open_ || entries_.empty()) {
        return;
    }
}

const std::string &MenuOverlay::focused_label() const
{
    static const std::string kEmpty;
    if (entries_.empty()) {
        return kEmpty;
    }
    return entries_[focus_index_];
}

} // namespace braillatron::ui
