/**
 * @file spec_tap.cpp
 * @implements SRS-HAL-001
 */
#include "satlink/hal/spec_tap.hpp"

#include "hal_error.hpp"

extern "C" {
#include <satlink/spec_tap.h>
}

namespace satlink::hal {
using detail::ThrowOnFailure;

SpecTapVersion SpecTap::GetVersion() const
{
    satlink_st_version raw{};
    ThrowOnFailure(io_.Ioctl(SATLINK_ST_IOC_GET_VERSION, &raw), "SATLINK_ST_IOC_GET_VERSION");
    return SpecTapVersion{.major = raw.major, .minor = raw.minor};
}

SpecTapCtrl SpecTap::GetCtrl() const
{
    satlink_st_ctrl raw{};
    ThrowOnFailure(io_.Ioctl(SATLINK_ST_IOC_GET_CTRL, &raw), "SATLINK_ST_IOC_GET_CTRL");
    return SpecTapCtrl{.enable = raw.enable != 0U, .window_en = raw.window_en != 0U};
}

void SpecTap::SetCtrl(const SpecTapCtrl &ctrl) const
{
    satlink_st_ctrl raw{};
    raw.enable = ctrl.enable ? 1U : 0U;
    raw.window_en = ctrl.window_en ? 1U : 0U;
    ThrowOnFailure(io_.Ioctl(SATLINK_ST_IOC_SET_CTRL, &raw), "SATLINK_ST_IOC_SET_CTRL");
}

SpecTapStatus SpecTap::GetStatus() const
{
    satlink_st_status raw{};
    ThrowOnFailure(io_.Ioctl(SATLINK_ST_IOC_GET_STATUS, &raw), "SATLINK_ST_IOC_GET_STATUS");
    return SpecTapStatus{.busy = raw.busy != 0U,
                         .frame_dropped = raw.frame_dropped != 0U,
                         .frame_cnt = raw.frame_cnt};
}

void SpecTap::ClearFrameDropped() const
{
    ThrowOnFailure(io_.Ioctl(SATLINK_ST_IOC_CLEAR_FRAME_DROP, nullptr),
                   "SATLINK_ST_IOC_CLEAR_FRAME_DROP");
}

} // namespace satlink::hal
