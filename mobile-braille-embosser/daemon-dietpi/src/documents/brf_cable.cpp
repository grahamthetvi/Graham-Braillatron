#include "brf_cable.h"

#include "brf_format.h"

#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <termios.h>
#include <unistd.h>

namespace braillatron::documents {

namespace {

speed_t baud_to_termios(uint32_t baud_rate)
{
    switch (baud_rate) {
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    default:
        return B115200;
    }
}

bool configure_port(int fd, uint32_t baud_rate)
{
    termios tty {};
    if (tcgetattr(fd, &tty) != 0) {
        return false;
    }
    cfmakeraw(&tty);
    cfsetispeed(&tty, baud_to_termios(baud_rate));
    cfsetospeed(&tty, baud_to_termios(baud_rate));
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    return tcsetattr(fd, TCSANOW, &tty) == 0;
}

std::string trim_copy(const std::string &value)
{
    size_t start = 0;
    while (start < value.size() &&
           std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

bool mostly_brf_text(const std::string &body)
{
    int cells = 0;
    int other = 0;
    for (unsigned char ch : body) {
        if (ch == '\n' || ch == '\r' || ch == '\f' || ch == '\t') {
            continue;
        }
        uint8_t mask = 0;
        if (brf_ascii_to_dot_mask(static_cast<char>(ch), &mask)) {
            ++cells;
        } else {
            ++other;
        }
    }
    const int total = cells + other;
    if (total == 0) {
        return false;
    }
    return cells * 5 >= total * 4;
}

} // namespace

BrfCableJob BrfCableParser::parse_buffer(std::string body) const
{
    BrfCableJob job;
    if (body.rfind("BRF1 ", 0) == 0 || body.rfind("BRF1\t", 0) == 0) {
        const size_t newline = body.find('\n');
        const std::string header = newline == std::string::npos ? body : body.substr(0, newline);
        job.filename = trim_copy(header.substr(4));
        body = newline == std::string::npos ? std::string {} : body.substr(newline + 1);
    }

    job.brf = normalize_brf_document(body);
    job.accepted = mostly_brf_text(job.brf);
    return job;
}

std::optional<BrfCableJob> BrfCableParser::finish(bool at_form_feed)
{
    std::string body;
    if (at_form_feed) {
        const size_t mark = buffer_.find('\f');
        body = buffer_.substr(0, mark);
        buffer_.erase(0, mark + 1);
    } else {
        body = std::move(buffer_);
        buffer_.clear();
    }
    saw_byte_ = !buffer_.empty();
    if (trim_copy(body).empty() && body.find_first_not_of(" \t\r\n") == std::string::npos) {
        return std::nullopt;
    }
    return parse_buffer(std::move(body));
}

std::optional<BrfCableJob> BrfCableParser::feed(const uint8_t *data, size_t len, uint64_t now_ms)
{
    if (data == nullptr || len == 0) {
        return poll(now_ms);
    }
    last_byte_ms_ = now_ms;
    saw_byte_ = true;
    buffer_.append(reinterpret_cast<const char *>(data), len);
    if (buffer_.size() > kMaxBytes) {
        return finish(false);
    }
    if (buffer_.find('\f') != std::string::npos) {
        return finish(true);
    }
    return std::nullopt;
}

std::optional<BrfCableJob> BrfCableParser::poll(uint64_t now_ms)
{
    if (!saw_byte_ || buffer_.empty()) {
        return std::nullopt;
    }
    if (buffer_.find('\f') != std::string::npos) {
        return finish(true);
    }
    if (now_ms - last_byte_ms_ >= kIdleCompleteMs) {
        return finish(false);
    }
    return std::nullopt;
}

BrfCableReceiver::BrfCableReceiver(std::string device_spec, uint32_t baud_rate,
                                   std::string avoid_device)
    : device_spec_(std::move(device_spec))
    , baud_rate_(baud_rate)
    , avoid_device_(std::move(avoid_device))
{
}

void BrfCableReceiver::set_enabled(bool enabled)
{
    enabled_ = enabled;
    if (!enabled_ && fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
        open_path_.clear();
        logged_open_ = false;
    }
}

std::string BrfCableReceiver::resolve_device() const
{
    namespace fs = std::filesystem;
    if (!device_spec_.empty() && device_spec_ != "auto") {
        if (device_spec_ == avoid_device_) {
            return {};
        }
        return device_spec_;
    }

    for (int index = 0; index < 8; ++index) {
        const std::string path = "/dev/ttyUSB" + std::to_string(index);
        std::error_code ec;
        if (!fs::exists(path, ec) || path == avoid_device_) {
            continue;
        }
        return path;
    }
    return {};
}

bool BrfCableReceiver::try_open(uint64_t now_ms)
{
    if (fd_ >= 0) {
        return true;
    }
    if (now_ms < next_open_attempt_ms_) {
        return false;
    }
    next_open_attempt_ms_ = now_ms + 2000;

    const std::string path = resolve_device();
    if (path.empty()) {
        return false;
    }

    const int opened = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (opened < 0) {
        return false;
    }
    if (!configure_port(opened, baud_rate_)) {
        ::close(opened);
        return false;
    }
    fd_ = opened;
    open_path_ = path;
    if (!logged_open_) {
        std::cerr << "[brf-cable] listening on " << path << " at " << baud_rate_ << "\n";
        logged_open_ = true;
    }
    return true;
}

std::vector<BrfCableJob> BrfCableReceiver::poll(uint64_t now_ms)
{
    std::vector<BrfCableJob> jobs;
    if (!enabled_) {
        return jobs;
    }
    if (!try_open(now_ms)) {
        return jobs;
    }

    uint8_t buf[1024];
    while (true) {
        const ssize_t n = ::read(fd_, buf, sizeof(buf));
        if (n > 0) {
            if (auto job = parser_.feed(buf, static_cast<size_t>(n), now_ms)) {
                jobs.push_back(std::move(*job));
            }
            continue;
        }
        if (n == 0 || (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
            break;
        }
        ::close(fd_);
        fd_ = -1;
        open_path_.clear();
        logged_open_ = false;
        std::cerr << "[brf-cable] port closed: " << std::strerror(errno) << "\n";
        break;
    }

    while (auto job = parser_.poll(now_ms)) {
        jobs.push_back(std::move(*job));
    }
    return jobs;
}

} // namespace braillatron::documents
