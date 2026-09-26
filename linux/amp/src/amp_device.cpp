/**
 * @file amp_device.cpp
 * @brief /dev/satlink-amp backend. Built only for SATLINK_TARGET=linux.
 *
 * @implements SRS-AMP-003
 */
#include "satlink/amp_client/amp_device.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <satlink/amp.h>
#include <sys/ioctl.h>
#include <system_error>
#include <unistd.h>

namespace satlink::amp {

AmpDevice::AmpDevice(const std::string &path)
{
    fd_ = ::open(path.c_str(), O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (fd_ < 0)
    {
        throw std::system_error(errno, std::generic_category(), path);
    }
}

AmpDevice::~AmpDevice()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
    }
}

int AmpDevice::Send(std::uint16_t type, std::span<const std::uint8_t> payload)
{
    if (payload.size() > SATLINK_AMP_MAX_PAYLOAD)
    {
        return -EINVAL;
    }
    std::array<std::uint8_t, sizeof(satlink_amp_msg_hdr) + SATLINK_AMP_MAX_PAYLOAD> buf{};
    const satlink_amp_msg_hdr hdr{type, static_cast<std::uint16_t>(payload.size())};
    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    std::memcpy(buf.data() + sizeof(hdr), payload.data(), payload.size());
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const ssize_t n = ::write(fd_, buf.data(), sizeof(hdr) + payload.size());
        if (n >= 0)
        {
            return 0;
        }
        if (errno != EAGAIN)
        {
            return -errno;
        }
        pollfd pfd{fd_, POLLOUT, 0};
        ::poll(&pfd, 1, 10);
    }
    return -EAGAIN;
}

int AmpDevice::Receive(Message &msg, std::chrono::milliseconds timeout)
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
    if ((pfd.revents & POLLERR) != 0)
    {
        return -ENODEV;
    }
    std::array<std::uint8_t, sizeof(satlink_amp_msg_hdr) + SATLINK_AMP_MAX_PAYLOAD> buf{};
    const ssize_t n = ::read(fd_, buf.data(), buf.size());
    if (n < 0)
    {
        return (errno == EAGAIN) ? -ETIMEDOUT : -errno;
    }
    satlink_amp_msg_hdr hdr{};
    std::memcpy(&hdr, buf.data(), sizeof(hdr));
    msg.type = hdr.type;
    msg.payload.assign(buf.begin() + sizeof(hdr), buf.begin() + sizeof(hdr) + hdr.len);
    return 0;
}

int AmpDevice::Start()
{
    return ::ioctl(fd_, SATLINK_AMP_IOC_START) == 0 ? 0 : -errno;
}

int AmpDevice::Stop()
{
    return ::ioctl(fd_, SATLINK_AMP_IOC_STOP) == 0 ? 0 : -errno;
}

int AmpDevice::GetStatus(satlink_amp_status &status)
{
    return ::ioctl(fd_, SATLINK_AMP_IOC_GET_STATUS, &status) == 0 ? 0 : -errno;
}

} // namespace satlink::amp
