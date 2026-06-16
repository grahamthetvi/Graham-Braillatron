#include "evdev_input.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace braillatron::keyboard {

namespace {

constexpr unsigned long kBitsPerLong = static_cast<unsigned long>(sizeof(unsigned long) * 8);

bool device_has_key_code(int fd, unsigned code)
{
    unsigned long key_bits[(KEY_MAX + static_cast<int>(kBitsPerLong)) /
                           static_cast<int>(kBitsPerLong)];
    std::memset(key_bits, 0, sizeof(key_bits));

    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        return false;
    }

    return (key_bits[code / kBitsPerLong] & (1UL << (code % kBitsPerLong))) != 0;
}

std::string device_input_name(const std::string &path)
{
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return {};
    }

    char name[256] = {};
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
        close(fd);
        return {};
    }
    close(fd);
    return name;
}

bool is_excluded_evdev_device(const std::string &name)
{
    // BRLTTY injects unrelated key events that corrupt Perkins chord assembly.
    return name.find("BRLTTY") != std::string::npos;
}

bool device_has_bench_keyboard(const std::string &path)
{
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }

    const bool has_perkins_row =
        device_has_key_code(fd, KEY_F) && device_has_key_code(fd, KEY_J);
    close(fd);
    return has_perkins_row;
}

void enqueue_key_event(EvdevKeymap &keymap, std::vector<EvdevKeyEvent> &pending_events,
                       std::mutex &queue_mutex, const input_event &ev)
{
    if (ev.type != EV_KEY || (ev.value != 0 && ev.value != 1)) {
        return;
    }

    if (!keymap.has_mapping(static_cast<unsigned>(ev.code))) {
        return;
    }

    EvdevKeyEvent event {};
    event.code = static_cast<unsigned>(ev.code);
    event.pressed = ev.value == 1;

    std::lock_guard<std::mutex> lock(queue_mutex);
    pending_events.push_back(event);
}

} // namespace

EvdevInput::EvdevInput(std::string device_path, bool grab_device)
    : device_path_(std::move(device_path))
    , grab_device_(grab_device)
{
}

EvdevInput::~EvdevInput()
{
    stop();
}

std::string EvdevInput::resolve_device_path(const std::string &configured_path)
{
    if (!configured_path.empty() && configured_path != "auto") {
        return configured_path;
    }

    for (int index = 0; index < 32; ++index) {
        const std::string candidate = "/dev/input/event" + std::to_string(index);
        if (device_has_bench_keyboard(candidate)) {
            return candidate;
        }
    }

    return {};
}

std::vector<std::string> EvdevInput::resolve_device_paths(const std::string &configured_path)
{
    if (!configured_path.empty() && configured_path != "auto") {
        return {configured_path};
    }

    std::vector<std::string> paths;
    for (int index = 0; index < 32; ++index) {
        const std::string candidate = "/dev/input/event" + std::to_string(index);
        if (!device_has_bench_keyboard(candidate)) {
            continue;
        }

        const std::string name = device_input_name(candidate);
        if (is_excluded_evdev_device(name)) {
            continue;
        }
        paths.push_back(candidate);
    }

    return paths;
}

bool EvdevInput::start(const EvdevKeymap &keymap)
{
    if (running_.load()) {
        return connected_.load();
    }

    if (device_path_.empty()) {
        connected_ = false;
        return false;
    }

    keymap_ = keymap;

    fd_ = open(device_path_.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd_ < 0) {
        std::cerr << "evdev: unable to open " << device_path_ << ": " << std::strerror(errno)
                  << "\n";
        connected_ = false;
        return false;
    }

    if (grab_device_) {
        if (ioctl(fd_, EVIOCGRAB, 1) < 0) {
            std::cerr << "evdev: EVIOCGRAB failed on " << device_path_ << ": "
                      << std::strerror(errno) << "\n";
        }
    }

    running_ = true;
    connected_ = true;
    worker_ = std::thread([this]() {
        while (running_.load()) {
            pollfd pfd {};
            pfd.fd = fd_;
            pfd.events = POLLIN;

            const int poll_result = poll(&pfd, 1, 100);
            if (poll_result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                break;
            }
            if (poll_result == 0) {
                continue;
            }

            while (running_.load()) {
                input_event ev {};
                const ssize_t nbytes = read(fd_, &ev, sizeof(ev));
                if (nbytes == static_cast<ssize_t>(sizeof(ev))) {
                    enqueue_key_event(keymap_, pending_events_, queue_mutex_, ev);
                    continue;
                }
                if (nbytes < 0 && (errno == EAGAIN || errno == EINTR)) {
                    break;
                }
                break;
            }
        }

        connected_ = false;
    });

    return true;
}

void EvdevInput::stop()
{
    if (!running_.load() && fd_ < 0) {
        return;
    }

    running_ = false;

    const int fd = fd_;
    fd_ = -1;
    if (fd >= 0) {
        if (grab_device_) {
            ioctl(fd, EVIOCGRAB, 0);
        }
        close(fd);
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    connected_ = false;
}

bool EvdevInput::is_connected() const
{
    return connected_.load();
}

void EvdevInput::drain_events(std::vector<EvdevKeyEvent> &out)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    out.insert(out.end(), pending_events_.begin(), pending_events_.end());
    pending_events_.clear();
}

} // namespace braillatron::keyboard
