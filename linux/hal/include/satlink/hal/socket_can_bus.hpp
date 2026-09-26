/**
 * @file socket_can_bus.hpp
 * @brief CanBus over a Linux SocketCAN raw socket (can0 = PmodCAN #1, mcp251x driver).
 *
 * @implements SRS-HKC-006
 * @implements SRS-PER-002
 */
#pragma once

#include <string>

#include "satlink/hal/can_bus.hpp"

namespace satlink::hal {

class SocketCanBus final : public CanBus
{
  public:
    /// Opens and binds a CAN_RAW socket on @p interface. Throws std::system_error on failure.
    explicit SocketCanBus(const std::string &interface);
    ~SocketCanBus() override;

    SocketCanBus(const SocketCanBus &) = delete;
    SocketCanBus &operator=(const SocketCanBus &) = delete;
    SocketCanBus(SocketCanBus &&) = delete;
    SocketCanBus &operator=(SocketCanBus &&) = delete;

    int Send(const satlink_can_frame_t &frame) override;
    int Receive(satlink_can_frame_t &frame, std::chrono::milliseconds timeout) override;

  private:
    int fd_ = -1;
};

} // namespace satlink::hal
