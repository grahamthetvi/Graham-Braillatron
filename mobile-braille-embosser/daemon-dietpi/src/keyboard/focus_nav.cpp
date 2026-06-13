#include "focus_nav.h"

#include <utility>

namespace braillatron::keyboard {

void FocusNavigator::set_entries(std::vector<std::string> entries)
{
    entries_ = std::move(entries);
    if (focus_index_ >= entries_.size()) {
        focus_index_ = entries_.empty() ? 0 : entries_.size() - 1;
    }
    notify_focus(false);
}

void FocusNavigator::on_dpad_up()
{
    if (entries_.empty() || focus_index_ == 0) {
        notify_focus(true);
        return;
    }
    --focus_index_;
    notify_focus(false);
}

void FocusNavigator::on_dpad_down()
{
    if (entries_.empty() || focus_index_ + 1 >= entries_.size()) {
        notify_focus(true);
        return;
    }
    ++focus_index_;
    notify_focus(false);
}

void FocusNavigator::on_backspace()
{
    if (!input_buffer_.empty()) {
        input_buffer_.pop_back();
    }
}

void FocusNavigator::on_enter()
{
    if (activate_handler_ && !entries_.empty()) {
        activate_handler_(focus_index_, entries_[focus_index_]);
    }
}

void FocusNavigator::on_text(const std::string &text)
{
    input_buffer_ += text;
}

size_t FocusNavigator::focus_index() const
{
    return focus_index_;
}

const std::string &FocusNavigator::focused_label() const
{
    static const std::string kEmpty;
    if (entries_.empty()) {
        return kEmpty;
    }
    return entries_[focus_index_];
}

const std::string &FocusNavigator::input_buffer() const
{
    return input_buffer_;
}

const std::vector<std::string> &FocusNavigator::entries() const
{
    return entries_;
}

void FocusNavigator::set_activate_handler(FocusActivateHandler handler)
{
    activate_handler_ = std::move(handler);
}

void FocusNavigator::set_focus_changed_handler(FocusChangedHandler handler)
{
    focus_changed_handler_ = std::move(handler);
}

void FocusNavigator::notify_focus(bool at_boundary)
{
    if (focus_changed_handler_) {
        focus_changed_handler_(focused_label(), at_boundary);
    }
}

} // namespace braillatron::keyboard
