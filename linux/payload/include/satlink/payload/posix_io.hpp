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
#include <functional>
#include <memory>
#include <optional>
#include <poll.h>
#include <string>
#include <string_view>
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

/**
 * @brief systemd service notifications (the sd_notify datagram protocol, without libsystemd).
 *        Does nothing when the process was not started by systemd.
 *
 * @implements SRS-FDIR-002
 */
class SystemdNotifier
{
  public:
    SystemdNotifier();
    ~SystemdNotifier();
    SystemdNotifier(const SystemdNotifier &) = delete;
    SystemdNotifier &operator=(const SystemdNotifier &) = delete;
    SystemdNotifier(SystemdNotifier &&) = delete;
    SystemdNotifier &operator=(SystemdNotifier &&) = delete;

    /// Sends "READY=1", "WATCHDOG=1", "STATUS=..." etc. Returns false if not under systemd.
    bool Notify(std::string_view state);
    /// WatchdogSec of the unit in ms (0: no watchdog). Ping at least twice per period.
    [[nodiscard]] std::uint64_t WatchdogMs() const
    {
        return watchdog_ms_;
    }

  private:
    int fd_ = -1;
    std::string path_;
    std::uint64_t watchdog_ms_ = 0;
};

/**
 * @brief Line-oriented TCP server (SCPI raw socket, port 5025). Each received line is passed to
 *        the handler; a non-empty answer is sent back with a newline. Several clients may be
 *        connected; lines longer than kMaxLine are dropped.
 */
class TcpLineServer
{
  public:
    using Handler = std::function<std::string(std::string_view)>;
    static constexpr std::size_t kMaxClients = 8;
    static constexpr std::size_t kMaxLine = 8192;

    TcpLineServer(std::uint16_t port, Handler handler);
    ~TcpLineServer();
    TcpLineServer(const TcpLineServer &) = delete;
    TcpLineServer &operator=(const TcpLineServer &) = delete;
    TcpLineServer(TcpLineServer &&) = delete;
    TcpLineServer &operator=(TcpLineServer &&) = delete;

    /// Appends the listening socket and the clients to a poll set.
    void AddPollFds(std::vector<pollfd> &fds) const;
    /// Accepts, reads and answers whatever is ready; never blocks.
    void Poll();

    [[nodiscard]] std::size_t Clients() const
    {
        return clients_.size();
    }
    /// The bound port (useful with port 0).
    [[nodiscard]] std::uint16_t Port() const
    {
        return port_;
    }

  private:
    struct Client
    {
        int fd;
        std::string in;
        bool overflow = false;
    };
    bool Serve(Client &c);

    int fd_ = -1;
    std::uint16_t port_ = 0;
    Handler handler_;
    std::vector<Client> clients_;
};

} // namespace satlink::payload
