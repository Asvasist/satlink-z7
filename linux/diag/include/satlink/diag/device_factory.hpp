/**
 * @file device_factory.hpp
 * @brief How the diagnostic commands obtain their devices.
 *
 * @implements SRS-DIAG-001
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "satlink/hal/char_device_io.hpp"
#include "satlink/hal/i2c_bus.hpp"

namespace satlink::diag {

/**
 * @brief Opens the device a command needs, only when the command runs.
 *
 * The real implementation (linux/diag/src/posix_device_factory.cpp) opens device nodes; host
 * unit tests return mocks. Both throw std::system_error if a device cannot be opened.
 */
class DeviceFactory
{
  public:
    virtual ~DeviceFactory() = default;

    virtual std::unique_ptr<hal::CharDeviceIo> OpenCharDevice(const std::string &path) = 0;
    virtual std::unique_ptr<hal::I2cBus> OpenI2c(const std::string &path, std::uint8_t address) = 0;
};

} // namespace satlink::diag
