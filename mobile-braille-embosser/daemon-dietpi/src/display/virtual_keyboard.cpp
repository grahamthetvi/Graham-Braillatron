#include "virtual_keyboard.h"
#include <linux/uinput.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <iostream>
#include <cstdint>

namespace braillatron::display {

VirtualKeyboard::VirtualKeyboard() {}

VirtualKeyboard::~VirtualKeyboard() {
    destroy();
}

bool VirtualKeyboard::init() {
    fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd_ < 0) {
        std::cerr << "[virtual_keyboard] Failed to open /dev/uinput: " << std::strerror(errno) << "\n";
        return false;
    }

    ioctl(fd_, UI_SET_EVBIT, EV_KEY);
    ioctl(fd_, UI_SET_EVBIT, EV_SYN);

    // Register our specific keys
    const int keys[] = {
        KEY_F, KEY_D, KEY_S, KEY_J, KEY_K, KEY_L,
        KEY_UP, KEY_DOWN, KEY_BACKSPACE, KEY_ENTER,
        KEY_GRAVE, KEY_TAB, KEY_RIGHTMETA
    };

    for (int key : keys) {
        ioctl(fd_, UI_SET_KEYBIT, key);
    }

    struct usetup {
        struct input_id id;
        char name[UINPUT_MAX_NAME_SIZE];
        uint32_t ff_effects_max;
    } usetup_struct;

    std::memset(&usetup_struct, 0, sizeof(usetup_struct));
    usetup_struct.id.bustype = BUS_USB;
    usetup_struct.id.vendor = 0x1234;
    usetup_struct.id.product = 0x5678;
    std::strncpy(usetup_struct.name, "Braillatron Virtual Web Keyboard", UINPUT_MAX_NAME_SIZE);

    // In modern kernels we use UI_DEV_SETUP. If not supported, we fall back to write() setup.
    if (ioctl(fd_, UI_DEV_SETUP, &usetup_struct) < 0) {
        // Fallback for older kernels/compat
        struct uinput_user_dev uudev;
        std::memset(&uudev, 0, sizeof(uudev));
        uudev.id.bustype = BUS_USB;
        uudev.id.vendor = 0x1234;
        uudev.id.product = 0x5678;
        std::strncpy(uudev.name, "Braillatron Virtual Web Keyboard", UINPUT_MAX_NAME_SIZE);
        if (write(fd_, &uudev, sizeof(uudev)) < 0) {
            std::cerr << "[virtual_keyboard] Failed to write uinput_user_dev: " << std::strerror(errno) << "\n";
            close(fd_);
            fd_ = -1;
            return false;
        }
    }

    if (ioctl(fd_, UI_DEV_CREATE) < 0) {
        std::cerr << "[virtual_keyboard] UI_DEV_CREATE failed: " << std::strerror(errno) << "\n";
        close(fd_);
        fd_ = -1;
        return false;
    }

    std::cerr << "[virtual_keyboard] Created virtual uinput keyboard\n";
    return true;
}

void VirtualKeyboard::send_key(int keycode, bool pressed) {
    if (fd_ < 0) return;

    struct input_event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.type = EV_KEY;
    ev.code = keycode;
    ev.value = pressed ? 1 : 0;
    if (write(fd_, &ev, sizeof(ev)) < 0) {
        std::cerr << "[virtual_keyboard] Failed to write key event: " << std::strerror(errno) << "\n";
        return;
    }

    std::memset(&ev, 0, sizeof(ev));
    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    if (write(fd_, &ev, sizeof(ev)) < 0) {
        std::cerr << "[virtual_keyboard] Failed to write SYN event: " << std::strerror(errno) << "\n";
    }
}

void VirtualKeyboard::destroy() {
    if (fd_ >= 0) {
        ioctl(fd_, UI_DEV_DESTROY);
        close(fd_);
        fd_ = -1;
        std::cerr << "[virtual_keyboard] Destroyed virtual uinput keyboard\n";
    }
}

} // namespace braillatron::display
