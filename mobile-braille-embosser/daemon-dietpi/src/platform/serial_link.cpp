#include "serial_link.h"

#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace braillatron::platform {

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

    return tcsetattr(fd, TCSANOW, &tty) == 0;
}

} // namespace

SerialLink::SerialLink(std::string device_path, uint32_t baud_rate)
    : device_path_(std::move(device_path))
    , baud_rate_(baud_rate)
{
}

SerialLink::~SerialLink()
{
    close();
}

bool SerialLink::is_open() const
{
    return fd_ >= 0;
}

bool SerialLink::try_open()
{
    if (fd_ >= 0) {
        return true;
    }

    fd_ = open(device_path_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        return false;
    }

    if (!configure_serial_port(fd_, baud_rate_)) {
        close();
        return false;
    }

    return true;
}

void SerialLink::close()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool SerialLink::write_frame(uint8_t opcode, const void *payload, uint8_t payload_len)
{
    if (fd_ < 0) {
        return false;
    }

    uint8_t frame[BRAILLATRON_FRAME_MAX_SIZE];
    const size_t frame_len = BRAILLATRON_FRAME_TOTAL_SIZE(payload_len);

    braillatron_frame_header_t *header = reinterpret_cast<braillatron_frame_header_t *>(frame);
    header->sync = BRAILLATRON_SYNC_BYTE;
    header->version = BRAILLATRON_PROTOCOL_VERSION;
    header->opcode = opcode;
    header->sequence_id = sequence_id_++;
    header->payload_len = payload_len;

    if (payload_len > 0 && payload != nullptr) {
        std::memcpy(frame + BRAILLATRON_FRAME_HEADER_SIZE, payload, payload_len);
    }

    const size_t crc_offset = BRAILLATRON_FRAME_HEADER_SIZE + payload_len;
    const uint16_t crc = braillatron_crc16(frame, crc_offset);
    frame[crc_offset] = static_cast<uint8_t>(crc & 0xFFu);
    frame[crc_offset + 1u] = static_cast<uint8_t>((crc >> 8) & 0xFFu);

    const ssize_t written = write(fd_, frame, frame_len);
    return written == static_cast<ssize_t>(frame_len);
}

bool SerialLink::send_heartbeat()
{
    return write_frame(static_cast<uint8_t>(BRAILLATRON_OP_HEARTBEAT), nullptr, 0);
}

} // namespace braillatron::platform
