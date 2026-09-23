/**
 * @file char_device_io.hpp
 * @brief Abstract char-device I/O, injected into every HAL class.
 *
 * @implements SRS-HAL-002
 */
#pragma once

namespace satlink::hal
{

/**
 * @brief The only thing a HAL class needs from the outside world: one ioctl-shaped call.
 *
 * Production code gets a PosixCharDevice (linux/hal/src/posix_char_device.cpp, built only for
 * SATLINK_TARGET=linux). Host unit tests get a GoogleMock fake, so HAL logic - argument
 * packing, error mapping, retry/timeout handling - is verified without a board attached.
 */
class CharDeviceIo
{
public:
    virtual ~CharDeviceIo() = default;

    /**
     * @brief Issue one ioctl.
     * @param request  Command, e.g. SATLINK_FA_IOC_GET_VERSION.
     * @param arg      Pointer to the command's argument struct (in/out, per the UAPI header).
     * @return 0 on success, a negative errno value on failure (mirrors ::ioctl()).
     */
    virtual int Ioctl(unsigned long request, void *arg) = 0;
};

} // namespace satlink::hal
