/**
 * @file i2c_bus.hpp
 * @brief Abstract I2C write channel to one already-addressed device, injected into HAL classes.
 *
 * @implements SRS-HAL-002
 */
#pragma once

#include <cstdint>
#include <span>

namespace satlink::hal {

/**
 * @brief A byte sink bound to one I2C target address.
 *
 * Production code gets an I2cDevBus (built only for SATLINK_TARGET=linux); host unit tests get a
 * GoogleMock fake and check the exact bytes that would go on the wire.
 */
class I2cBus
{
  public:
    virtual ~I2cBus() = default;

    /// One complete I2C write transaction. Returns 0 on success, a negative errno on failure.
    virtual int Write(std::span<const std::uint8_t> data) = 0;
};

} // namespace satlink::hal
