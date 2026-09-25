/**
 * @file posix_io.hpp
 * @brief Linux implementations of the payload manager's interfaces: UDP ground link, TUN
 *        devices, CAN housekeeping, system clock. Built for SATLINK_TARGET=linux and for Linux
 *        hosts (the simulator and the HIL tests run satlink-payloadd on the PC).
 *
 * @implements SRS-PLM-003
 */
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "satlink/hal/can_bus.hpp"
#include "satlink/payload/interfaces.hpp"

namespace satlink::payload {

class SystemClock final : public Clock
{
  public:
    std::uint64_t NowMs() override;
    std::uint64_t UnixMs() override;
};

/**
 * @brief UDP: TCs arrive on @p tc_port; TM goes to the ground station address, which is the
 *        configured one or, if none, the sender of the last TC (answer whoever commands).
 */
class UdpGroundLink final : public GroundLink
{
  public:
    UdpGroundLink(std::uint16_t tc_port, const std::string &gs_host, std::uint16_t tm_port);
    ~UdpGroundLink() override;
    UdpGroundLink(const UdpGroundLink &) = delete;
    UdpGroundLink &operator=(const UdpGroundLink &) = delete;
    UdpGroundLink(UdpGroundLink &&) = delete;
    UdpGroundLink &operator=(UdpGroundLink &&) = delete;

    void SendTm(std::span<const std::uint8_t> packet) override;
    std::optional<std::vector<std::uint8_t>> ReceiveTc() override;
    [[nodiscard]] int Fd() const
    {
        return fd_;
    }

  private:
    int fd_ = -1;
    bool fixed_destination_ = false;
    std::uint16_t tm_port_;
    struct Destination;
    std::unique_ptr<Destination> dest_;
};

/** A TUN interface (IFF_TUN | IFF_NO_PI); address and routes are set up outside. */
class TunDevice final : public PacketTunnel
{
  public:
    explicit TunDevice(const std::string &name);
    ~TunDevice() override;
    TunDevice(const TunDevice &) = delete;
    TunDevice &operator=(const TunDevice &) = delete;
    TunDevice(TunDevice &&) = delete;
    TunDevice &operator=(TunDevice &&) = delete;

    std::optional<std::vector<std::uint8_t>> Read() override;
    void Write(std::span<const std::uint8_t> packet) override;
    [[nodiscard]] int Fd() const
    {
        return fd_;
    }
    [[nodiscard]] const std::string &Name() const
    {
        return name_;
    }

    [[nodiscard]] std::uint32_t WriteErrors() const
    {
        return write_errors_;
    }

  private:
    int fd_ = -1;
    std::string name_;
    std::uint32_t write_errors_ = 0;
};

/** Collects the housekeeping controller's CAN frames; Poll() from the main loop. */
class CanPlatformSource final : public PlatformSource
{
  public:
    explicit CanPlatformSource(std::unique_ptr<hal::CanBus> bus) : bus_(std::move(bus)) {}
    void Poll();
    PlatformHk Read() override;

  private:
    std::unique_ptr<hal::CanBus> bus_;
    PlatformHk hk_;
};

} // namespace satlink::payload
