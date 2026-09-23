/**
 * @file spec_tap.hpp
 * @brief C++20 HAL for the spec_tap driver (linux/drivers/spec_tap).
 *
 * @implements SRS-HAL-001
 */
#pragma once

#include <cstdint>

#include "satlink/hal/block_version.hpp"
#include "satlink/hal/char_device_io.hpp"

namespace satlink::hal {

using SpecTapVersion = BlockVersion;

struct SpecTapCtrl
{
    bool enable;
    bool window_en;
};

struct SpecTapStatus
{
    bool busy;
    bool frame_dropped;
    std::uint32_t frame_cnt;
};

/**
 * @brief Typed wrapper around the spec_tap char device.
 *
 * Every method issues exactly one ioctl(). Throws std::system_error (built from the driver's
 * -errno) on failure.
 */
class SpecTap
{
  public:
    /// @p io must outlive this object; it is not owned.
    explicit SpecTap(CharDeviceIo &io) : io_(io) {}

    [[nodiscard]] SpecTapVersion GetVersion() const;
    [[nodiscard]] SpecTapCtrl GetCtrl() const;
    void SetCtrl(const SpecTapCtrl &ctrl) const;
    [[nodiscard]] SpecTapStatus GetStatus() const;

    /// Clears the sticky FRAME_DROPPED flag.
    void ClearFrameDropped() const;

  private:
    CharDeviceIo &io_;
};

} // namespace satlink::hal
