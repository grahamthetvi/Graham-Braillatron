#include "focus_nav.h"

#include <cctype>
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
        notify_input_changed();
    }
}

void FocusNavigator::on_enter()
{
    if (activate_handler_ && !entries_.empty()) {
        activate_handler_(focus_index_, entries_[focus_index_]);
    }
}

void FocusNavigator::jump_to_letter(char letter)
{
    const unsigned char target =
        static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(letter)));
    if (target < 'a' || target > 'z') {
        return;
    }

    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].empty()) {
            continue;
        }
        const unsigned char first =
            static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(entries_[i][0])));
        if (first == target) {
            focus_index_ = i;
            notify_focus(false);
            return;
        }
    }

    notify_focus(true);
}

void FocusNavigator::on_text(const std::string &text)
{
    if (!text.empty()) {
        const unsigned char ch = static_cast<unsigned char>(text[0]);
        if (std::isalpha(ch)) {
            input_buffer_.clear();
            jump_to_letter(static_cast<char>(ch));
            return;
        }
    }

    input_buffer_ += text;
    notify_input_changed();
}

void FocusNavigator::clear_input_buffer()
{
    if (input_buffer_.empty()) {
        return;
    }
    input_buffer_.clear();
    notify_input_changed();
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

void FocusNavigator::set_input_changed_handler(InputChangedHandler handler)
{
    input_changed_handler_ = std::move(handler);
}

void FocusNavigator::notify_focus(bool at_boundary)
{
    if (focus_changed_handler_) {
        focus_changed_handler_(focused_label(), at_boundary);
    }
}

void FocusNavigator::notify_input_changed()
{
    if (input_changed_handler_) {
        input_changed_handler_();
    }
}

} // namespace braillatron::keyboard
