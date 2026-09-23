/**
 * @file ccsds_frame_accel.cpp
 * @implements SRS-HAL-001
 */
#include "satlink/hal/ccsds_frame_accel.hpp"

#include <system_error>

extern "C" {
#include <satlink/ccsds_frame_accel.h>
}

namespace satlink::hal {
namespace {

void ThrowOnFailure(int rc, const char *what)
{
    if (rc != 0)
    {
        throw std::system_error(-rc, std::generic_category(), what);
    }
}

} // namespace

FrameAccelVersion FrameAccelerator::GetVersion() const
{
    satlink_fa_version raw{};
    ThrowOnFailure(io_.Ioctl(SATLINK_FA_IOC_GET_VERSION, &raw), "SATLINK_FA_IOC_GET_VERSION");
    return FrameAccelVersion{.major = raw.major, .minor = raw.minor};
}

FrameAccelCtrl FrameAccelerator::GetCtrl() const
{
    satlink_fa_ctrl raw{};
    ThrowOnFailure(io_.Ioctl(SATLINK_FA_IOC_GET_CTRL, &raw), "SATLINK_FA_IOC_GET_CTRL");
    return FrameAccelCtrl{.randomizer_en = raw.randomizer_en != 0, .irq_en = raw.irq_en != 0};
}

void FrameAccelerator::SetCtrl(const FrameAccelCtrl &ctrl) const
{
    satlink_fa_ctrl raw{};
    raw.randomizer_en = ctrl.randomizer_en ? 1U : 0U;
    raw.irq_en = ctrl.irq_en ? 1U : 0U;
    raw.reserved[0] = 0U;
    raw.reserved[1] = 0U;
    ThrowOnFailure(io_.Ioctl(SATLINK_FA_IOC_SET_CTRL, &raw), "SATLINK_FA_IOC_SET_CTRL");
}

std::optional<FrameStats> FrameAccelerator::WaitFrame(std::chrono::milliseconds timeout) const
{
    satlink_fa_wait_frame raw{};
    raw.timeout_ms = static_cast<std::uint32_t>(timeout.count());
    raw.timed_out = 0U;
    ThrowOnFailure(io_.Ioctl(SATLINK_FA_IOC_WAIT_FRAME, &raw), "SATLINK_FA_IOC_WAIT_FRAME");

    if (raw.timed_out != 0U)
    {
        return std::nullopt;
    }
    return FrameStats{.crc = raw.stats.crc,
                      .last_len_bytes = raw.stats.last_len_bytes,
                      .frame_cnt = raw.stats.frame_cnt};
}

} // namespace satlink::hal
