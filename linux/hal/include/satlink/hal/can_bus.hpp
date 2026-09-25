/**
 * @file can_bus.hpp
 * @brief Abstract classic-CAN endpoint, injected into the CAN-facing tools.
 *
 * @implements SRS-HAL-002
 * @implements SRS-HKC-006
 */
#pragma once

#include <chrono>

#include "satlink/hkc/can_frame.h"

namespace satlink::hal {

/**
 * @brief Sends and receives classic CAN frames with 11-bit identifiers.
 *
 * Production code gets a SocketCanBus (SATLINK_TARGET=linux); host tests get fakes that talk to
 * the bootloader and housekeeping logic directly.
 */
class CanBus
{
  public:
    virtual ~CanBus() = default;

    /// Queues one frame. Returns 0 on success, a negative errno on failure.
    virtual int Send(const satlink_can_frame_t &frame) = 0;

    /// Waits up to @p timeout for one frame. Returns 0 when @p frame was filled, -ETIMEDOUT when
    /// nothing arrived, another negative errno on failure.
    virtual int Receive(satlink_can_frame_t &frame, std::chrono::milliseconds timeout) = 0;
};

} // namespace satlink::hal
