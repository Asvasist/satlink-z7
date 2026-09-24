/**
 * @file socket_can_port.cpp
 * @implements SRS-HAL-002
 */
#include "satlink/hal/socket_can_port.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>

namespace satlink::hal {
namespace {

[[noreturn]] void ThrowErrno(const std::string &what)
{
    throw std::system_error(errno, std::generic_category(), what);
}

void Configure(int fd, const std::string &interface, const std::vector<std::uint32_t> &accept_ids)
{
    if (interface.empty() || interface.size() >= IFNAMSIZ)
    {
        throw std::system_error(ENAMETOOLONG, std::generic_category(),
                                "CAN interface name '" + interface + "'");
    }

    ifreq request{};
    std::copy(interface.begin(), interface.end(), static_cast<char *>(request.ifr_name));
    if (::ioctl(fd, SIOCGIFINDEX, &request) < 0)
    {
        ThrowErrno("CAN interface " + interface);
    }

    if (!accept_ids.empty())
    {
        std::vector<can_filter> filters;
        filters.reserve(accept_ids.size());
        for (const std::uint32_t id : accept_ids)
        {
            // Match the whole identifier, and only standard data frames.
            filters.push_back(
                can_filter{id & CAN_SFF_MASK, CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG});
        }
        if (::setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FILTER, filters.data(),
                         static_cast<socklen_t>(filters.size() * sizeof(can_filter))) < 0)
        {
            ThrowErrno("setsockopt(CAN_RAW_FILTER)");
        }
    }

    sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = request.ifr_ifindex;
    if (::bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) < 0)
    {
        ThrowErrno("bind(" + interface + ")");
    }
}

} // namespace

SocketCanPort::SocketCanPort(const std::string &interface,
                             const std::vector<std::uint32_t> &accept_ids)
    : fd_(::socket(PF_CAN, SOCK_RAW, CAN_RAW))
{
    if (fd_ < 0)
    {
        ThrowErrno("socket(PF_CAN)");
    }
    try
    {
        Configure(fd_, interface, accept_ids);
    }
    catch (...)
    {
        ::close(fd_);
        throw;
    }
}

SocketCanPort::~SocketCanPort()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
    }
}

void SocketCanPort::Send(const CanFrame &frame)
{
    can_frame raw{};
    raw.can_id = frame.id & CAN_SFF_MASK;
    raw.can_dlc = std::min<std::uint8_t>(frame.dlc, CAN_MAX_DLEN);
    std::copy_n(frame.data.begin(), raw.can_dlc, static_cast<std::uint8_t *>(raw.data));

    // A full transmit queue is a matter of milliseconds on a working bus; wait a little.
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        const ssize_t written = ::write(fd_, &raw, sizeof(raw));
        if (written == static_cast<ssize_t>(sizeof(raw)))
        {
            return;
        }
        if (written >= 0 || (errno != ENOBUFS && errno != EAGAIN && errno != EINTR))
        {
            ThrowErrno("write(CAN frame)");
        }
        pollfd waiting{fd_, POLLOUT, 0};
        (void)::poll(&waiting, 1, 20);
    }
    throw std::system_error(ENOBUFS, std::generic_category(), "write(CAN frame)");
}

std::optional<CanFrame> SocketCanPort::Receive(std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;)
    {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        pollfd waiting{fd_, POLLIN, 0};
        const int ready = ::poll(&waiting, 1, static_cast<int>(std::max(left.count(), 0LL)));
        if (ready < 0 && errno != EINTR)
        {
            ThrowErrno("poll(CAN)");
        }
        if (ready <= 0)
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return std::nullopt;
            }
            continue;
        }

        can_frame raw{};
        const ssize_t got = ::read(fd_, &raw, sizeof(raw));
        if (got != static_cast<ssize_t>(sizeof(raw)))
        {
            if (got < 0 && (errno == EAGAIN || errno == EINTR))
            {
                continue;
            }
            ThrowErrno("read(CAN frame)");
        }
        if ((raw.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG)) != 0U)
        {
            continue; // only standard data frames matter here
        }

        CanFrame frame;
        frame.id = raw.can_id & CAN_SFF_MASK;
        frame.dlc = std::min<std::uint8_t>(raw.can_dlc, CAN_MAX_DLEN);
        std::copy_n(static_cast<const std::uint8_t *>(raw.data), frame.dlc, frame.data.begin());
        return frame;
    }
}

} // namespace satlink::hal
