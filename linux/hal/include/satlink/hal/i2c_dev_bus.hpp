/**
 * @file i2c_dev_bus.hpp
 * @brief Real I2cBus backend: a Linux i2c-dev node bound to one target address.
 *
 * Built only for SATLINK_TARGET=linux (see linux/hal/CMakeLists.txt).
 *
 * @implements SRS-HAL-002
 */
#pragma once

#include <cstdint>
#include <string>

#include "satlink/hal/i2c_bus.hpp"

namespace satlink::hal {

/// RAII wrapper around an open /dev/i2c-N descriptor with the target address selected.
class I2cDevBus final : public I2cBus
{
  public:
    /// Throws std::system_error if the node cannot be opened or the address cannot be selected.
    I2cDevBus(const std::string &path, std::uint8_t address);
    ~I2cDevBus() override;

    I2cDevBus(const I2cDevBus &) = delete;
    I2cDevBus &operator=(const I2cDevBus &) = delete;

    int Write(std::span<const std::uint8_t> data) override;

  private:
    int fd_;
};

} // namespace satlink::hal
