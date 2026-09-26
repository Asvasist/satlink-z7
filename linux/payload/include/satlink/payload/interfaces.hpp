/**
 * @file interfaces.hpp
 * @brief What the payload manager needs from its surroundings. Linux implementations live in
 *        linux/payload/src (UDP, TUN, SocketCAN); host tests use fakes.
 *
 * @implements SRS-PLM-001
 */
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace satlink::payload {

class Clock
{
  public:
    virtual ~Clock() = default;
    /// Monotonic milliseconds.
    virtual std::uint64_t NowMs() = 0;
    /// Wall clock, Unix milliseconds (for TM time stamps and the firmware time).
    virtual std::uint64_t UnixMs() = 0;
};

/// The ground segment's network link (UDP on the board).
class GroundLink
{
  public:
    virtual ~GroundLink() = default;
    virtual void SendTm(std::span<const std::uint8_t> packet) = 0;
    /// Next received TC datagram, or nullopt (never blocks).
    virtual std::optional<std::vector<std::uint8_t>> ReceiveTc() = 0;
};

/// An IP endpoint (TUN device on the board).
class PacketTunnel
{
  public:
    virtual ~PacketTunnel() = default;
    virtual std::optional<std::vector<std::uint8_t>> Read() = 0;
    virtual void Write(std::span<const std::uint8_t> packet) = 0;
};

/// Housekeeping from outside the modem (the MicroBlaze V over CAN, the AMP driver).
struct PlatformHk
{
    bool hkc_valid = false;
    std::int16_t hkc_temp_centi_c = 0;
    std::uint16_t hkc_vccint_mv = 0;
    std::uint16_t hkc_vccaux_mv = 0;
    std::uint16_t hkc_vbram_mv = 0;
    std::uint32_t hkc_uptime_s = 0;
    std::uint8_t hkc_error_flags = 0;
    std::uint8_t rtos_state = 0;
    std::uint32_t rtos_restarts = 0;
};

class PlatformSource
{
  public:
    virtual ~PlatformSource() = default;
    virtual PlatformHk Read() = 0;
};

} // namespace satlink::payload
