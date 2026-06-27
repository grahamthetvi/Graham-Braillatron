#include "layered_browse_list.h"

#include "output_hub.h"

#include <algorithm>
#include <utility>

namespace braillatron::ui {

void LayeredBrowseList::set_items(std::vector<BrowseListItem> items, size_t focus_index)
{
    items_ = std::move(items);
    focus_index_ = focus_index;
    clamp_focus();
}

void LayeredBrowseList::set_items(std::vector<std::string> labels, size_t focus_index)
{
    std::vector<BrowseListItem> items;
    items.reserve(labels.size());
    for (std::string &label : labels) {
        items.push_back(BrowseListItem {std::move(label), nullptr, nullptr});
    }
    set_items(std::move(items), focus_index);
}

void LayeredBrowseList::clear()
{
    items_.clear();
    focus_index_ = 0;
}

void LayeredBrowseList::set_container_name(const std::string &name)
{
    container_name_ = name;
}

bool LayeredBrowseList::empty() const
{
    return items_.empty();
}

size_t LayeredBrowseList::size() const
{
    return items_.size();
}

size_t LayeredBrowseList::focus_index() const
{
    return focus_index_;
}

std::string LayeredBrowseList::focused_label() const
{
    if (items_.empty() || focus_index_ >= items_.size()) {
        return {};
    }
    return resolve_label(items_[focus_index_]);
}

std::vector<std::string> LayeredBrowseList::labels() const
{
    std::vector<std::string> resolved;
    resolved.reserve(items_.size());
    for (const BrowseListItem &item : items_) {
        resolved.push_back(resolve_label(item));
    }
    return resolved;
}

bool LayeredBrowseList::move_up()
{
    if (focus_index_ == 0 || items_.empty()) {
        return false;
    }
    --focus_index_;
    return true;
}

bool LayeredBrowseList::move_down()
{
    if (items_.empty() || focus_index_ + 1 >= items_.size()) {
        return false;
    }
    ++focus_index_;
    return true;
}

bool LayeredBrowseList::activate()
{
    if (items_.empty() || focus_index_ >= items_.size()) {
        return false;
    }

    const BrowseListItem &item = items_[focus_index_];
    if (item.on_activate) {
        item.on_activate();
        return true;
    }
    return false;
}

void LayeredBrowseList::set_focus(size_t index)
{
    focus_index_ = index;
    clamp_focus();
}

void LayeredBrowseList::reset_focus()
{
    focus_index_ = 0;
    clamp_focus();
}

std::string LayeredBrowseList::position_label() const
{
    if (items_.empty()) {
        return {};
    }
    return std::to_string(focus_index_ + 1) + " of " + std::to_string(items_.size());
}

bool LayeredBrowseList::handle_control(keyboard::ControlKey key, OutputHub *output)
{
    if (items_.empty()) {
        return false;
    }

    switch (key) {
    case keyboard::ControlKey::DpadUp: {
        const size_t before = focus_index_;
        move_up();
        announce_focus(output, focus_index_ == before);
        return true;
    }
    case keyboard::ControlKey::DpadDown: {
        const size_t before = focus_index_;
        move_down();
        announce_focus(output, focus_index_ == before);
        return true;
    }
    case keyboard::ControlKey::Enter:
        activate();
        return true;
    default:
        return false;
    }
}

void LayeredBrowseList::announce_focus(OutputHub *output, bool at_boundary)
{
    if (output == nullptr) {
        return;
    }
    output->announce_list_focus(focused_accessible_node(), at_boundary);
}

AccessibleElement LayeredBrowseList::focused_accessible_node() const
{
    AccessibleElement elem;
    elem.role = "Menu Item";
    elem.state = "Focused";
    elem.container = container_name_;

    if (items_.empty() || focus_index_ >= items_.size()) {
        return elem;
    }

    const BrowseListItem &item = items_[focus_index_];
    elem.name = resolve_label(item);
    elem.index = static_cast<int>(focus_index_);
    elem.count = static_cast<int>(items_.size());
    return elem;
}

void LayeredBrowseList::clamp_focus()
{
    if (items_.empty()) {
        focus_index_ = 0;
    } else if (focus_index_ >= items_.size()) {
        focus_index_ = items_.size() - 1;
    }
}

std::string LayeredBrowseList::resolve_label(const BrowseListItem &item) const
{
    if (item.dynamic_label) {
        return item.dynamic_label();
    }
    return item.label;
}

} // namespace braillatron::ui
