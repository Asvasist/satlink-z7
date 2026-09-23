/**
 * @file block_version.hpp
 * @brief Version of a PL block, as read from its VERSION register.
 *
 * @implements SRS-HAL-001
 */
#pragma once

#include <cstdint>

namespace satlink::hal {

struct BlockVersion
{
    std::uint16_t major;
    std::uint16_t minor;
};

} // namespace satlink::hal
