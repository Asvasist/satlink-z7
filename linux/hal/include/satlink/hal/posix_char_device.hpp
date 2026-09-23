/**
 * @file posix_char_device.hpp
 * @brief Real CharDeviceIo backend: a POSIX char device opened by path.
 *
 * Built only for SATLINK_TARGET=linux (see linux/hal/CMakeLists.txt) - the host build never
 * compiles this, so it is the one place in the HAL allowed to call open()/ioctl()/close().
 *
 * @implements SRS-HAL-002
 */
#pragma once

#include <string>

#include "satlink/hal/char_device_io.hpp"

namespace satlink::hal {

/// RAII wrapper around a single open character-device file descriptor.
class PosixCharDevice final : public CharDeviceIo
{
  public:
    /// Throws std::system_error if @p path cannot be opened.
    explicit PosixCharDevice(const std::string &path);
    ~PosixCharDevice() override;

    PosixCharDevice(const PosixCharDevice &) = delete;
    PosixCharDevice &operator=(const PosixCharDevice &) = delete;

    int Ioctl(unsigned long request, void *arg) override;

  private:
    int fd_;
};

} // namespace satlink::hal
