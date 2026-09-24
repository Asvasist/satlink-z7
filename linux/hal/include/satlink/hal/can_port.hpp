/**
 * @file can_port.hpp
 * @brief Abstract CAN interface, injected into the classes that talk on the bus.
 *
 * @implements SRS-HAL-002
 */
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>

namespace satlink::hal {

/// One classic CAN frame with a standard 11-bit identifier.
struct CanFrame
{
    std::uint32_t id = 0;
    std::uint8_t dlc = 0;
    std::array<std::uint8_t, 8> data{};

    friend bool operator==(const CanFrame &, const CanFrame &) = default;
};

/**
 * @brief Sends and receives CAN frames.
 *
 * Production code gets a SocketCanPort (linux/hal/src/socket_can_port.cpp, built only for
 * SATLINK_TARGET=linux). Host unit tests get a fake bus, sometimes with the real bootloader
 * receiver behind it. Both throw std::system_error if the bus fails.
 */
class CanPort
{
  public:
    virtual ~CanPort() = default;

    /// Puts one frame on the bus.
    virtual void Send(const CanFrame &frame) = 0;

    /// Waits up to @p timeout for a frame; std::nullopt if none arrived.
    virtual std::optional<CanFrame> Receive(std::chrono::milliseconds timeout) = 0;
};

} // namespace satlink::hal
