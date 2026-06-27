#pragma once

#include "accessible_output.h"

#include "../keyboard/chord_engine.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace braillatron::ui {

class OutputHub;

struct BrowseListItem {
    std::string label;
    std::function<std::string()> dynamic_label;
    std::function<void()> on_activate;
};

// Flat Up/Down browsable list for in-app menus (launcher-style visual list).
class LayeredBrowseList {
public:
    void set_items(std::vector<BrowseListItem> items, size_t focus_index = 0);
    void set_items(std::vector<std::string> labels, size_t focus_index = 0);
    void clear();
    void set_container_name(const std::string &name);

    bool empty() const;
    size_t size() const;
    size_t focus_index() const;
    std::string focused_label() const;
    std::vector<std::string> labels() const;

    bool move_up();
    bool move_down();
    bool activate();
    void set_focus(size_t index);
    void reset_focus();
    std::string position_label() const;

    bool handle_control(keyboard::ControlKey key, OutputHub *output);
    void announce_focus(OutputHub *output, bool at_boundary = false);
    AccessibleElement focused_accessible_node() const;

private:
    void clamp_focus();
    std::string resolve_label(const BrowseListItem &item) const;

    std::vector<BrowseListItem> items_;
    size_t focus_index_ = 0;
    std::string container_name_;
};

} // namespace braillatron::ui
