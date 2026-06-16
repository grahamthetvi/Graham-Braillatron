#pragma once

namespace braillatron::display {

class VirtualKeyboard {
public:
    VirtualKeyboard();
    ~VirtualKeyboard();

    bool init();
    void send_key(int keycode, bool pressed);
    void destroy();

private:
    int fd_ = -1;
};

} // namespace braillatron::display
