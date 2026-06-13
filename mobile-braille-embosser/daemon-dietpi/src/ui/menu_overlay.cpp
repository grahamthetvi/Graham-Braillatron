#include "menu_overlay.h"

#include "display/ui_chrome_model.h"

#include <utility>

namespace braillatron::ui {

MenuOverlay::MenuOverlay() = default;

void MenuOverlay::set_root_items(std::vector<MenuItem> items)
{
    root_items_ = std::move(items);
}

bool MenuOverlay::is_open() const
{
    return open_;
}

void MenuOverlay::open()
{
    open_ = true;
    stack_.clear();
    if (!root_items_.empty()) {
        stack_.push_back(MenuLevel {root_items_, 0});
    }
    refresh_resolved_label();
}

void MenuOverlay::close()
{
    open_ = false;
    stack_.clear();
    resolved_label_.clear();
}

void MenuOverlay::move_up()
{
    if (!open_ || stack_.empty()) {
        return;
    }

    MenuLevel &level = current_level();
    if (level.items.empty() || level.focus_index == 0) {
        return;
    }

    --level.focus_index;
    refresh_resolved_label();
}

void MenuOverlay::move_down()
{
    if (!open_ || stack_.empty()) {
        return;
    }

    MenuLevel &level = current_level();
    if (level.items.empty() || level.focus_index + 1 >= level.items.size()) {
        return;
    }

    ++level.focus_index;
    refresh_resolved_label();
}

void MenuOverlay::activate()
{
    if (!open_ || stack_.empty()) {
        return;
    }

    const MenuLevel &level = current_level();
    if (level.items.empty()) {
        return;
    }

    const MenuItem &item = level.items[level.focus_index];
    if (item.on_activate) {
        item.on_activate(*this);
        refresh_resolved_label();
    }
}

bool MenuOverlay::push_level(std::vector<MenuItem> items)
{
    if (!open_ || items.empty()) {
        return false;
    }

    stack_.push_back(MenuLevel {std::move(items), 0});
    refresh_resolved_label();
    return true;
}

bool MenuOverlay::pop_level()
{
    if (!open_ || stack_.size() <= 1) {
        return false;
    }

    stack_.pop_back();
    refresh_resolved_label();
    return true;
}

size_t MenuOverlay::depth() const
{
    return stack_.size();
}

const std::string &MenuOverlay::focused_label() const
{
    return resolved_label_;
}

void MenuOverlay::refresh_resolved_label()
{
    if (!open_ || stack_.empty()) {
        resolved_label_.clear();
        return;
    }

    const MenuLevel &level = current_level();
    if (level.items.empty()) {
        resolved_label_.clear();
        return;
    }

    const MenuItem &item = level.items[level.focus_index];
    if (item.dynamic_label) {
        resolved_label_ = item.dynamic_label();
        return;
    }

    resolved_label_ = item.label;
}

MenuLevel &MenuOverlay::current_level()
{
    return stack_.back();
}

const MenuLevel &MenuOverlay::current_level() const
{
    return stack_.back();
}

size_t MenuOverlay::focus_index() const
{
    if (!open_ || stack_.empty()) {
        return 0;
    }
    return current_level().focus_index;
}

std::vector<std::string> MenuOverlay::current_item_labels() const
{
    std::vector<std::string> labels;
    if (!open_ || stack_.empty()) {
        return labels;
    }

    const MenuLevel &level = current_level();
    labels.reserve(level.items.size());
    for (const MenuItem &item : level.items) {
        labels.push_back(resolve_menu_item_label(item));
    }
    return labels;
}

} // namespace braillatron::ui
