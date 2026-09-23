/**
 * @file posix_char_device.cpp
 * @implements SRS-HAL-002
 */
#include "satlink/hal/posix_char_device.hpp"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <system_error>

namespace satlink::hal
{

PosixCharDevice::PosixCharDevice(const std::string &path) : fd_(::open(path.c_str(), O_RDWR))
{
    if (fd_ < 0)
    {
        throw std::system_error(errno, std::generic_category(), "open(" + path + ")");
    }
}

PosixCharDevice::~PosixCharDevice()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
    }
}

int PosixCharDevice::Ioctl(unsigned long request, void *arg)
{
    if (::ioctl(fd_, request, arg) < 0)
    {
        return -errno;
    }
    return 0;
}

} // namespace satlink::hal
