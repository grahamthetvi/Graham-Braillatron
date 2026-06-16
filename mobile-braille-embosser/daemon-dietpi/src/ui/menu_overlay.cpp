#include "menu_overlay.h"

#include "display/ui_chrome_model.h"

#include <utility>

namespace braillatron::ui {

namespace {
std::string trim_spaces(const std::string &value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}
} // namespace

AccessibleElement MenuItem::get_accessible_node() const
{
    AccessibleElement elem;
    elem.role = role.empty() ? "Menu Item" : role;
    elem.state = "Focused";

    if (value_provider) {
        elem.name = label;
        elem.value = value_provider();
    } else {
        std::string resolved = dynamic_label ? dynamic_label() : label;
        size_t colon_pos = resolved.find(':');
        if (colon_pos != std::string::npos) {
            elem.name = trim_spaces(resolved.substr(0, colon_pos));
            elem.value = trim_spaces(resolved.substr(colon_pos + 1));
        } else {
            size_t paren_pos = resolved.find('(');
            if (paren_pos != std::string::npos) {
                elem.name = trim_spaces(resolved.substr(0, paren_pos));
                elem.value = trim_spaces(resolved.substr(paren_pos));
            } else {
                elem.name = resolved;
            }
        }
    }
    return elem;
}

MenuOverlay::MenuOverlay() = default;

void MenuOverlay::set_root_items(std::vector<MenuItem> items, const std::string &name)
{
    root_items_ = std::move(items);
    root_level_name_ = name.empty() ? "Menu" : name;
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
        stack_.push_back(MenuLevel {root_items_, 0, root_level_name_});
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
        activating_item_label_ = item.label;
        item.on_activate(*this);
        activating_item_label_.clear();
        refresh_resolved_label();
    }
}

bool MenuOverlay::push_level(std::vector<MenuItem> items, const std::string &name)
{
    if (!open_ || items.empty()) {
        return false;
    }

    std::string level_name = name;
    if (level_name.empty()) {
        level_name = activating_item_label_;
    }
    if (level_name.empty()) {
        level_name = "Submenu";
    }

    stack_.push_back(MenuLevel {std::move(items), 0, level_name});
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

std::string MenuOverlay::current_level_name() const
{
    if (!open_ || stack_.empty()) {
        return "";
    }
    return stack_.back().name;
}

AccessibleElement MenuOverlay::focused_accessible_node() const
{
    if (!open_ || stack_.empty()) {
        return {};
    }

    const MenuLevel &level = current_level();
    if (level.items.empty() || level.focus_index >= level.items.size()) {
        return {};
    }

    const MenuItem &item = level.items[level.focus_index];
    AccessibleElement elem = item.get_accessible_node();
    elem.container = current_level_name();
    elem.index = level.focus_index;
    elem.count = level.items.size();
    return elem;
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
