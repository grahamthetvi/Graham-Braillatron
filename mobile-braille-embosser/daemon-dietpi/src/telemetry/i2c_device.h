#pragma once

#include <cstdint>
#include <string>

namespace braillatron::telemetry {

class I2cDevice {
public:
    I2cDevice(std::string bus_path, uint8_t address);
    ~I2cDevice();

    I2cDevice(const I2cDevice &) = delete;
    I2cDevice &operator=(const I2cDevice &) = delete;

    bool is_open() const;
    bool write_register(uint8_t reg, uint8_t value);
    bool read_register(uint8_t reg, uint8_t &value);
    bool read_register16(uint8_t reg, uint16_t &value);

private:
    std::string bus_path_;
    uint8_t address_ = 0;
    int fd_ = -1;
};

} // namespace braillatron::telemetry
