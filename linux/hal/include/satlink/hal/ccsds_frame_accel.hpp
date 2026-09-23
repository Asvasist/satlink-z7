/**
 * @file ccsds_frame_accel.hpp
 * @brief C++20 HAL for the ccsds_frame_accel driver (linux/drivers/ccsds_frame_accel).
 *
 * @implements SRS-HAL-001
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "satlink/hal/block_version.hpp"
#include "satlink/hal/char_device_io.hpp"

namespace satlink::hal {

using FrameAccelVersion = BlockVersion;

struct FrameAccelCtrl
{
    bool randomizer_en;
    bool irq_en;
};

struct FrameStats
{
    std::uint16_t crc;
    std::uint32_t last_len_bytes;
    std::uint32_t frame_cnt;
};

/**
 * @brief Typed wrapper around the ccsds_frame_accel char device.
 *
 * Every method issues exactly one ioctl(); callers never see raw ioctl numbers or the UAPI
 * struct layout. Throws std::system_error (built from the driver's -errno) on any failure
 * except a WaitFrame() timeout, which is a normal outcome and returns std::nullopt instead.
 */
class FrameAccelerator
{
  public:
    /// @p io must outlive this object; it is not owned.
    explicit FrameAccelerator(CharDeviceIo &io) : io_(io) {}

    [[nodiscard]] FrameAccelVersion GetVersion() const;
    [[nodiscard]] FrameAccelCtrl GetCtrl() const;
    void SetCtrl(const FrameAccelCtrl &ctrl) const;

    /// Blocks until the next FRAME_DONE, or returns std::nullopt once @p timeout elapses.
    /// A zero-duration timeout blocks forever (matches SATLINK_FA_IOC_WAIT_FRAME's timeout_ms
    /// == 0 convention).
    [[nodiscard]] std::optional<FrameStats> WaitFrame(std::chrono::milliseconds timeout) const;

  private:
    CharDeviceIo &io_;
};

} // namespace satlink::hal
