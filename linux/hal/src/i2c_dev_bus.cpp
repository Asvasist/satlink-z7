/**
 * @file i2c_dev_bus.cpp
 * @implements SRS-HAL-002
 */
#include "satlink/hal/i2c_dev_bus.hpp"

#include <cerrno>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <system_error>
#include <unistd.h>

namespace satlink::hal {

I2cDevBus::I2cDevBus(const std::string &path, std::uint8_t address)
    : fd_(::open(path.c_str(), O_RDWR))
{
    if (fd_ < 0)
    {
        throw std::system_error(errno, std::generic_category(), "open(" + path + ")");
    }
    if (::ioctl(fd_, I2C_SLAVE, static_cast<unsigned long>(address)) < 0)
    {
        const int saved_errno = errno;
        ::close(fd_);
        throw std::system_error(saved_errno, std::generic_category(), "ioctl(I2C_SLAVE)");
    }
}

I2cDevBus::~I2cDevBus()
{
    if (fd_ >= 0)
    {
        ::close(fd_);
    }
}

int I2cDevBus::Write(std::span<const std::uint8_t> data)
{
    const ssize_t written = ::write(fd_, data.data(), data.size());
    if (written < 0)
    {
        return -errno;
    }
    if (static_cast<std::size_t>(written) != data.size())
    {
        return -EIO;
    }
    return 0;
}

} // namespace satlink::hal
