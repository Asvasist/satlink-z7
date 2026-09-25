/**
 * @file message_port.hpp
 * @brief Abstract message channel to the modem firmware on Core 1.
 *
 * On the board it is /dev/satlink-amp (AmpDevice); in host builds and tests it is a
 * SimulatedCore1 that runs the real modem application in-process.
 *
 * @implements SRS-AMP-003
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <vector>

namespace satlink::amp {

struct Message
{
    std::uint16_t type = 0;
    std::vector<std::uint8_t> payload;
};

class MessagePort
{
  public:
    virtual ~MessagePort() = default;

    /// Queue one message for the firmware. 0 on success, a negative errno on failure.
    virtual int Send(std::uint16_t type, std::span<const std::uint8_t> payload) = 0;

    /// Wait up to @p timeout for one message. 0 when @p msg was filled, -ETIMEDOUT, or another
    /// negative errno.
    virtual int Receive(Message &msg, std::chrono::milliseconds timeout) = 0;
};

} // namespace satlink::amp
