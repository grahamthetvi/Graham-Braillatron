#include "i2c_device.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace braillatron::telemetry {

I2cDevice::I2cDevice(std::string bus_path, uint8_t address)
    : bus_path_(std::move(bus_path))
    , address_(address)
{
    fd_ = open(bus_path_.c_str(), O_RDWR);
    if (fd_ < 0) {
        return;
    }

    if (ioctl(fd_, I2C_SLAVE, address_) < 0) {
        close(fd_);
        fd_ = -1;
    }
}

I2cDevice::~I2cDevice()
{
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool I2cDevice::is_open() const
{
    return fd_ >= 0;
}

bool I2cDevice::write_register(uint8_t reg, uint8_t value)
{
    if (fd_ < 0) {
        return false;
    }

    const uint8_t buffer[2] = {reg, value};
    return write(fd_, buffer, sizeof(buffer)) == static_cast<ssize_t>(sizeof(buffer));
}

bool I2cDevice::read_register(uint8_t reg, uint8_t &value)
{
    if (fd_ < 0) {
        return false;
    }

    if (write(fd_, &reg, 1) != 1) {
        return false;
    }

    return read(fd_, &value, 1) == 1;
}

bool I2cDevice::read_register16(uint8_t reg, uint16_t &value)
{
    uint8_t msb = 0;
    uint8_t lsb = 0;
    if (!read_register(reg, msb) || !read_register(static_cast<uint8_t>(reg + 1u), lsb)) {
        return false;
    }

    value = static_cast<uint16_t>((static_cast<uint16_t>(msb) << 8) | lsb);
    return true;
}

} // namespace braillatron::telemetry
