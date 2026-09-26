/**
 * @file socket_can_bus.cpp
 * @brief SocketCAN backend. Built only for SATLINK_TARGET=linux.
 *
 * @implements SRS-HKC-006
 * @implements SRS-PER-002
 */
#include "satlink/hal/socket_can_bus.hpp"

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

SocketCanBus::SocketCanBus(const std::string &interface)
{
    fd_ = ::socket(PF_CAN, SOCK_RAW | SOCK_CLOEXEC, CAN_RAW);
    if (fd_ < 0)
    {
        throw std::system_error(errno, std::generic_category(), "socket(PF_CAN)");
    }

    ifreq ifr{};
    if (interface.size() >= sizeof(ifr.ifr_name))
    {
        ::close(fd_);
        throw std::system_error(ENAMETOOLONG, std::generic_category(), interface);
    }
    std::memcpy(ifr.ifr_name, interface.c_str(), interface.size() + 1);
    if (::ioctl(fd_, SIOCGIFINDEX, &ifr) < 0)
    {
        const int err = errno;
        ::close(fd_);
        throw std::system_error(err, std::generic_category(), "SIOCGIFINDEX " + interface);
    }

    sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): sockets API
    if (::bind(fd_, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        const int err = errno;
        ::close(fd_);
        throw std::system_error(err, std::generic_category(), "bind " + interface);
    }
}

SocketCanBus::~SocketCanBus()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
    }
}

int SocketCanBus::Send(const satlink_can_frame_t &frame)
{
    if ((frame.id > SATLINK_CAN_MAX_STD_ID) || (frame.dlc > SATLINK_CAN_MAX_DLC))
    {
        return -EINVAL;
    }
    can_frame out{};
    out.can_id = frame.id;
    out.len = frame.dlc;
    std::memcpy(out.data, frame.data, frame.dlc);
    const ssize_t n = ::write(fd_, &out, sizeof(out));
    if (n < 0)
    {
        return -errno;
    }
    return (n == static_cast<ssize_t>(sizeof(out))) ? 0 : -EIO;
}

int SocketCanBus::Receive(satlink_can_frame_t &frame, std::chrono::milliseconds timeout)
{
    for (;;)
    {
        pollfd pfd{fd_, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
        if (ready < 0)
        {
            return (errno == EINTR) ? -ETIMEDOUT : -errno;
        }
        if (ready == 0)
        {
            return -ETIMEDOUT;
        }
        can_frame in{};
        const ssize_t n = ::read(fd_, &in, sizeof(in));
        if (n < 0)
        {
            return -errno;
        }
        if ((n != static_cast<ssize_t>(sizeof(in))) ||
            ((in.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG)) != 0U))
        {
            continue; // not a standard data frame: not part of the SatLink protocols
        }
        frame.id = static_cast<std::uint16_t>(in.can_id & CAN_SFF_MASK);
        frame.dlc = (in.len > SATLINK_CAN_MAX_DLC) ? SATLINK_CAN_MAX_DLC : in.len;
        std::memset(frame.data, 0, sizeof(frame.data));
        std::memcpy(frame.data, in.data, frame.dlc);
        return 0;
    }
}

} // namespace satlink::hal
