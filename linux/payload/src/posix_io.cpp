/**
 * @file posix_io.cpp
 * @implements SRS-PLM-003
 * @implements SRS-NET-001
 */
#include "satlink/payload/posix_io.hpp"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>

#include "satlink/hkc/hk_proto.h"

namespace satlink::payload {

std::uint64_t SystemClock::NowMs()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

std::uint64_t SystemClock::UnixMs()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
}

struct UdpGroundLink::Destination
{
    sockaddr_in addr{};
    bool valid = false;
};

UdpGroundLink::UdpGroundLink(std::uint16_t tc_port, const std::string &gs_host,
                             std::uint16_t tm_port)
    : tm_port_(tm_port), dest_(std::make_unique<Destination>())
{
    fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0)
    {
        throw std::system_error(errno, std::generic_category(), "socket(UDP)");
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(tc_port);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): sockets API
    if (::bind(fd_, reinterpret_cast<const sockaddr *>(&local), sizeof(local)) < 0)
    {
        const int err = errno;
        ::close(fd_);
        throw std::system_error(err, std::generic_category(),
                                "bind UDP " + std::to_string(tc_port));
    }
    if (!gs_host.empty())
    {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo *res = nullptr;
        if (::getaddrinfo(gs_host.c_str(), nullptr, &hints, &res) != 0 || res == nullptr)
        {
            ::close(fd_);
            throw std::system_error(EINVAL, std::generic_category(), "resolve " + gs_host);
        }
        std::memcpy(&dest_->addr, res->ai_addr, sizeof(sockaddr_in));
        ::freeaddrinfo(res);
        dest_->addr.sin_port = htons(tm_port_);
        dest_->valid = true;
        fixed_destination_ = true;
    }
}

UdpGroundLink::~UdpGroundLink()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
    }
}

void UdpGroundLink::SendTm(std::span<const std::uint8_t> packet)
{
    if (!dest_->valid)
    {
        return; // nobody has commanded yet and no station is configured
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): sockets API
    (void)::sendto(fd_, packet.data(), packet.size(), 0,
                   reinterpret_cast<const sockaddr *>(&dest_->addr), sizeof(dest_->addr));
}

std::optional<std::vector<std::uint8_t>> UdpGroundLink::ReceiveTc()
{
    std::vector<std::uint8_t> buf(65536);
    sockaddr_in from{};
    socklen_t len = sizeof(from);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): sockets API
    const ssize_t n =
        ::recvfrom(fd_, buf.data(), buf.size(), 0, reinterpret_cast<sockaddr *>(&from), &len);
    if (n < 0)
    {
        return std::nullopt;
    }
    if (!fixed_destination_)
    {
        dest_->addr = from;
        dest_->addr.sin_port = htons(tm_port_);
        dest_->valid = true;
    }
    buf.resize(static_cast<std::size_t>(n));
    return buf;
}

TunDevice::TunDevice(const std::string &name) : name_(name)
{
    fd_ = ::open("/dev/net/tun", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0)
    {
        throw std::system_error(errno, std::generic_category(), "/dev/net/tun");
    }
    ifreq ifr{};
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    std::strncpy(ifr.ifr_name, name.c_str(), IFNAMSIZ - 1);
    if (::ioctl(fd_, TUNSETIFF, &ifr) < 0)
    {
        const int err = errno;
        ::close(fd_);
        throw std::system_error(err, std::generic_category(), "TUNSETIFF " + name);
    }
    name_ = ifr.ifr_name;
}

TunDevice::~TunDevice()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
    }
}

std::optional<std::vector<std::uint8_t>> TunDevice::Read()
{
    std::vector<std::uint8_t> buf(2048);
    const ssize_t n = ::read(fd_, buf.data(), buf.size());
    if (n <= 0)
    {
        return std::nullopt;
    }
    buf.resize(static_cast<std::size_t>(n));
    return buf;
}

void TunDevice::Write(std::span<const std::uint8_t> packet)
{
    // A datagram the kernel refuses (interface down, no buffer) is dropped, like on any IP link.
    if (::write(fd_, packet.data(), packet.size()) < 0)
    {
        ++write_errors_;
    }
}

void CanPlatformSource::Poll()
{
    for (int i = 0; i < 32; ++i)
    {
        satlink_can_frame_t frame{};
        if (bus_->Receive(frame, std::chrono::milliseconds{0}) != 0)
        {
            return;
        }
        satlink_hk_env_t env{};
        satlink_hk_status_t status{};
        if (satlink_hk_decode_env(&frame, &env) == SATLINK_OK)
        {
            hk_.hkc_valid = true;
            hk_.hkc_temp_centi_c = env.die_temp_centi_c;
            hk_.hkc_vccint_mv = env.vccint_mv;
            hk_.hkc_vccaux_mv = env.vccaux_mv;
            hk_.hkc_vbram_mv = env.vbram_mv;
        }
        else if (satlink_hk_decode_status(&frame, &status) == SATLINK_OK)
        {
            hk_.hkc_uptime_s = status.uptime_s;
            hk_.hkc_error_flags = status.error_flags;
        }
    }
}

PlatformHk CanPlatformSource::Read()
{
    return hk_;
}

// ---- systemd ----

SystemdNotifier::SystemdNotifier()
{
    // NOLINTNEXTLINE(concurrency-mt-unsafe): read once at start-up, before any threads
    const char *socket_path = std::getenv("NOTIFY_SOCKET");
    if (socket_path == nullptr || socket_path[0] == '\0')
    {
        return;
    }
    path_ = socket_path;
    if (path_[0] == '@')
    {
        path_[0] = '\0'; // abstract namespace
    }
    fd_ = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    // NOLINTNEXTLINE(concurrency-mt-unsafe): as above
    const char *usec = std::getenv("WATCHDOG_USEC");
    if (usec != nullptr)
    {
        watchdog_ms_ = std::strtoull(usec, nullptr, 10) / 1000U;
    }
}

SystemdNotifier::~SystemdNotifier()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
    }
}

bool SystemdNotifier::Notify(std::string_view state)
{
    sockaddr_un addr{};
    if (fd_ < 0 || path_.size() >= sizeof(addr.sun_path))
    {
        return false;
    }
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path_.data(), path_.size());
    const auto len = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path_.size());
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): sockets API
    return ::sendto(fd_, state.data(), state.size(), MSG_NOSIGNAL,
                    reinterpret_cast<const sockaddr *>(&addr),
                    len) == static_cast<ssize_t>(state.size());
}

// ---- TCP line server ----

TcpLineServer::TcpLineServer(std::uint16_t port, Handler handler) : handler_(std::move(handler))
{
    fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0)
    {
        throw std::system_error(errno, std::generic_category(), "socket(TCP)");
    }
    const int one = 1;
    (void)::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);
    socklen_t len = sizeof(local);
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast): sockets API
    if (::bind(fd_, reinterpret_cast<const sockaddr *>(&local), sizeof(local)) < 0 ||
        ::listen(fd_, 4) < 0 || ::getsockname(fd_, reinterpret_cast<sockaddr *>(&local), &len) < 0)
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
    {
        const int err = errno;
        ::close(fd_);
        throw std::system_error(err, std::generic_category(), "listen TCP " + std::to_string(port));
    }
    port_ = ntohs(local.sin_port);
}

TcpLineServer::~TcpLineServer()
{
    for (const auto &c : clients_)
    {
        ::close(c.fd);
    }
    ::close(fd_);
}

void TcpLineServer::AddPollFds(std::vector<pollfd> &fds) const
{
    fds.push_back({fd_, POLLIN, 0});
    for (const auto &c : clients_)
    {
        fds.push_back({c.fd, POLLIN, 0});
    }
}

void TcpLineServer::Poll()
{
    for (;;)
    {
        const int fd = ::accept4(fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0)
        {
            break;
        }
        if (clients_.size() >= kMaxClients)
        {
            ::close(fd);
            continue;
        }
        const int one = 1;
        (void)::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        clients_.push_back({fd, {}, false});
    }
    for (auto it = clients_.begin(); it != clients_.end();)
    {
        if (Serve(*it))
        {
            ++it;
        }
        else
        {
            ::close(it->fd);
            it = clients_.erase(it);
        }
    }
}

bool TcpLineServer::Serve(Client &c)
{
    std::array<char, 1024> buf{};
    for (;;)
    {
        const ssize_t n = ::recv(c.fd, buf.data(), buf.size(), 0);
        if (n == 0)
        {
            return false; // closed by the peer
        }
        if (n < 0)
        {
            return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        }
        for (std::size_t i = 0; i < static_cast<std::size_t>(n); ++i)
        {
            const char ch = buf[i];
            if (ch != '\n')
            {
                if (c.in.size() < kMaxLine)
                {
                    c.in += ch;
                }
                else
                {
                    c.overflow = true;
                }
                continue;
            }
            if (!c.overflow)
            {
                std::string answer = handler_(c.in);
                if (!answer.empty())
                {
                    answer += '\n';
                    // Answers are short; a client that does not read them loses the connection.
                    if (::send(c.fd, answer.data(), answer.size(), MSG_NOSIGNAL) !=
                        static_cast<ssize_t>(answer.size()))
                    {
                        return false;
                    }
                }
            }
            c.in.clear();
            c.overflow = false;
        }
    }
}

} // namespace satlink::payload
