#include "serial_listener.h"

extern "C" {
#include "protocol.h"
}

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

namespace braillatron::keyboard {

namespace {

enum class ParseState {
    Sync,
    Header,
    Payload,
};

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

bool configure_serial_port(int fd, uint32_t baud_rate)
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
    tty.c_cc[VTIME] = 1;

    return tcsetattr(fd, TCSANOW, &tty) == 0;
}

bool opcode_accepted(uint8_t opcode, size_t payload_size)
{
    switch (opcode) {
    case BRAILLATRON_OP_KEYBOARD_MATRIX:
        return payload_size == sizeof(braillatron_keyboard_matrix_t);
    case BRAILLATRON_OP_CHORD:
        return payload_size == sizeof(braillatron_chord_event_t);
    case BRAILLATRON_OP_SAFETY:
        return payload_size == sizeof(braillatron_safety_broadcast_t);
    default:
        return false;
    }
}

class FrameParser {
public:
    void reset()
    {
        state_ = ParseState::Sync;
        header_len_ = 0;
        payload_len_ = 0;
        expected_payload_ = 0;
    }

    std::optional<SerialFrame> push_byte(uint8_t byte)
    {
        switch (state_) {
        case ParseState::Sync:
            if (byte != BRAILLATRON_SYNC_BYTE) {
                return std::nullopt;
            }
            buffer_[0] = byte;
            header_len_ = 1;
            state_ = ParseState::Header;
            return std::nullopt;

        case ParseState::Header:
            buffer_[header_len_++] = byte;
            if (header_len_ < BRAILLATRON_FRAME_HEADER_SIZE) {
                return std::nullopt;
            }

            if (buffer_[1] != BRAILLATRON_PROTOCOL_VERSION) {
                reset();
                return std::nullopt;
            }

            expected_payload_ = buffer_[4];
            if (expected_payload_ > BRAILLATRON_FRAME_MAX_PAYLOAD) {
                reset();
                return std::nullopt;
            }

            payload_len_ = 0;
            state_ = ParseState::Payload;
            if (expected_payload_ == 0) {
                return finalize_frame();
            }
            return std::nullopt;

        case ParseState::Payload:
            buffer_[BRAILLATRON_FRAME_HEADER_SIZE + payload_len_++] = byte;
            if (payload_len_ < expected_payload_ + BRAILLATRON_FRAME_CRC_SIZE) {
                return std::nullopt;
            }
            return finalize_frame();
        }

        return std::nullopt;
    }

private:
    std::optional<SerialFrame> finalize_frame()
    {
        const size_t frame_len =
            BRAILLATRON_FRAME_HEADER_SIZE + expected_payload_ + BRAILLATRON_FRAME_CRC_SIZE;
        const uint16_t expected_crc =
            braillatron_crc16(buffer_, frame_len - BRAILLATRON_FRAME_CRC_SIZE);
        const uint16_t received_crc =
            static_cast<uint16_t>(buffer_[frame_len - 2]) |
            (static_cast<uint16_t>(buffer_[frame_len - 1]) << 8);

        const uint8_t opcode = buffer_[2];
        const size_t payload_size = expected_payload_;
        reset();

        if (expected_crc != received_crc) {
            return std::nullopt;
        }

        if (!opcode_accepted(opcode, payload_size)) {
            return std::nullopt;
        }

        SerialFrame frame;
        frame.opcode = opcode;
        frame.payload_len = payload_size;
        std::memcpy(frame.payload.data(), buffer_ + BRAILLATRON_FRAME_HEADER_SIZE,
                    payload_size);
        return frame;
    }

    ParseState state_ = ParseState::Sync;
    uint8_t buffer_[BRAILLATRON_FRAME_MAX_SIZE] {};
    size_t header_len_ = 0;
    size_t payload_len_ = 0;
    size_t expected_payload_ = 0;
};

} // namespace

SerialListener::SerialListener(std::string device_path, uint32_t baud_rate)
    : device_path_(std::move(device_path))
    , baud_rate_(baud_rate)
{
}

SerialListener::~SerialListener()
{
    stop();
}

bool SerialListener::is_connected() const
{
    return connected_.load();
}

void SerialListener::set_disconnect_handler(SerialDisconnectHandler handler)
{
    disconnect_handler_ = std::move(handler);
}

bool SerialListener::start(FrameHandler handler)
{
    if (running_.load()) {
        return connected_.load();
    }

    handler_ = std::move(handler);
    fd_ = open(device_path_.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        connected_ = false;
        return false;
    }

    if (!configure_serial_port(fd_, baud_rate_)) {
        close(fd_);
        fd_ = -1;
        connected_ = false;
        return false;
    }

    running_ = true;
    connected_ = true;
    worker_ = std::thread([this]() {
        FrameParser parser;
        std::vector<uint8_t> chunk(256);
        bool disconnect_reported = false;

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

            const ssize_t nbytes = read(fd_, chunk.data(), chunk.size());
            if (nbytes < 0) {
                if (errno == EAGAIN || errno == EINTR) {
                    continue;
                }
                break;
            }
            if (nbytes == 0) {
                continue;
            }

            for (ssize_t i = 0; i < nbytes; ++i) {
                if (auto frame = parser.push_byte(chunk[static_cast<size_t>(i)])) {
                    if (handler_) {
                        handler_(*frame);
                    }
                }
            }
        }

        connected_ = false;
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }

        if (!disconnect_reported) {
            disconnect_reported = true;
            std::cerr << "[serial] disconnected from " << device_path_ << "\n";
            if (disconnect_handler_) {
                disconnect_handler_();
            }
        }
    });

    return true;
}

bool SerialListener::try_reconnect()
{
    if (connected_.load()) {
        return true;
    }

    stop();
    return start(handler_);
}

void SerialListener::stop()
{
    if (!running_.load() && fd_ < 0) {
        return;
    }

    running_ = false;

    const int fd = fd_;
    fd_ = -1;
    if (fd >= 0) {
        close(fd);
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    connected_ = false;
}

} // namespace braillatron::keyboard
