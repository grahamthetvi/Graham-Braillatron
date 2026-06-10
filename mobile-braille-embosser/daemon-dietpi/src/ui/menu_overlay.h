#pragma once

#include <functional>
#include <string>
#include <vector>

namespace braillatron::ui {

class MenuOverlay;

struct MenuItem {
    std::string label;
    std::function<std::string()> dynamic_label;
    std::function<void(MenuOverlay &)> on_activate;
};

struct MenuLevel {
    std::vector<MenuItem> items;
    size_t focus_index = 0;
};

class MenuOverlay {
public:
    MenuOverlay();

    void set_root_items(std::vector<MenuItem> items);

    bool is_open() const;
    void open();
    void close();
    void move_up();
    void move_down();
    void activate();

    bool push_level(std::vector<MenuItem> items);
    bool pop_level();

    size_t depth() const;
    const std::string &focused_label() const;

private:
    void refresh_resolved_label();
    MenuLevel &current_level();
    const MenuLevel &current_level() const;

    std::vector<MenuItem> root_items_;
    std::vector<MenuLevel> stack_;
    std::string resolved_label_;
    bool open_ = false;
};

} // namespace braillatron::ui
