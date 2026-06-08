#pragma once

#include <string>
#include <vector>

namespace braillatron::ui {

class MenuOverlay {
public:
    explicit MenuOverlay(std::vector<std::string> entries);

    bool is_open() const;
    void open();
    void close();
    void move_up();
    void move_down();
    void activate();

    const std::string &focused_label() const;

private:
    std::vector<std::string> entries_;
    size_t focus_index_ = 0;
    bool open_ = false;
};

} // namespace braillatron::ui
