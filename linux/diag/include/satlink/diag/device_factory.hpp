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
#include <vector>

#include "satlink/hal/can_port.hpp"
#include "satlink/hal/char_device_io.hpp"
#include "satlink/hal/i2c_bus.hpp"

namespace satlink::diag {

/**
 * @brief Opens the device a command needs, only when the command runs.
 *
 * The real implementation (linux/diag/src/main.cpp) opens device nodes; host unit tests return
 * mocks. Both throw std::system_error if a device cannot be opened.
 */
class DeviceFactory
{
  public:
    virtual ~DeviceFactory() = default;

    virtual std::unique_ptr<hal::CharDeviceIo> OpenCharDevice(const std::string &path) = 0;
    virtual std::unique_ptr<hal::I2cBus> OpenI2c(const std::string &path, std::uint8_t address) = 0;

    /// Opens a CAN interface such as "can0". Only frames with an identifier in @p accept_ids
    /// need to be delivered.
    virtual std::unique_ptr<hal::CanPort> OpenCan(const std::string &interface,
                                                  const std::vector<std::uint32_t> &accept_ids) = 0;

    /// Reads a whole file (a firmware image); throws std::system_error if it cannot be read.
    virtual std::vector<std::uint8_t> ReadFile(const std::string &path) = 0;
};

} // namespace satlink::diag
