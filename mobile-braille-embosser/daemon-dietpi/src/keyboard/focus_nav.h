#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace braillatron::keyboard {

using FocusActivateHandler = std::function<void(size_t index, const std::string &label)>;
using FocusChangedHandler = std::function<void(const std::string &label, bool at_boundary)>;

class FocusNavigator {
public:
    void set_entries(std::vector<std::string> entries);
    void on_dpad_up();
    void on_dpad_down();
    void on_backspace();
    void on_enter();
    void on_text(const std::string &text);

    size_t focus_index() const;
    const std::string &focused_label() const;
    const std::string &input_buffer() const;

    void set_activate_handler(FocusActivateHandler handler);
    void set_focus_changed_handler(FocusChangedHandler handler);

private:
    void notify_focus(bool at_boundary);

    std::vector<std::string> entries_;
    size_t focus_index_ = 0;
    std::string input_buffer_;
    FocusActivateHandler activate_handler_;
    FocusChangedHandler focus_changed_handler_;
};

} // namespace braillatron::keyboard
