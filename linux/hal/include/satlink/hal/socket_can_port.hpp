/**
 * @file socket_can_port.hpp
 * @brief Real CanPort backend: a SocketCAN raw socket on a network interface such as can0.
 *
 * Built only for SATLINK_TARGET=linux (see linux/hal/CMakeLists.txt); with posix_char_device.cpp
 * and i2c_dev_bus.cpp it is one of the few files that call into the OS.
 *
 * @implements SRS-HAL-002
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "satlink/hal/can_port.hpp"

namespace satlink::hal {

/// RAII wrapper around one PF_CAN raw socket, bound to one interface.
class SocketCanPort final : public CanPort
{
  public:
    /**
     * @param interface  Network interface name, for example "can0".
     * @param accept_ids Standard identifiers to receive; every other frame is dropped by the
     *                   kernel. Empty accepts all frames.
     * Throws std::system_error if the socket cannot be created or bound.
     */
    SocketCanPort(const std::string &interface, const std::vector<std::uint32_t> &accept_ids);
    ~SocketCanPort() override;

    SocketCanPort(const SocketCanPort &) = delete;
    SocketCanPort &operator=(const SocketCanPort &) = delete;

    void Send(const CanFrame &frame) override;
    std::optional<CanFrame> Receive(std::chrono::milliseconds timeout) override;

  private:
    int fd_;
};

} // namespace satlink::hal
