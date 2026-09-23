/**
 * @file hal_error.hpp
 * @brief Private helper shared by the HAL implementation files.
 */
#pragma once

#include <system_error>

namespace satlink::hal::detail {

/// Turns the negative-errno convention of CharDeviceIo / I2cBus into std::system_error.
inline void ThrowOnFailure(int rc, const char *what)
{
    if (rc != 0)
    {
        throw std::system_error(-rc, std::generic_category(), what);
    }
}

} // namespace satlink::hal::detail
