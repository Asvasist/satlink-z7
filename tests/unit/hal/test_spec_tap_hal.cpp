/**
 * @file test_spec_tap_hal.cpp
 * @brief SpecTap logic against a mocked CharDeviceIo - no board required.
 *
 * @verifies SRS-HAL-001
 * @verifies SRS-HAL-002
 * @verifies SRS-DRV-002
 */
#include <cerrno>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <system_error>

#include "satlink/hal/char_device_io.hpp"
#include "satlink/hal/spec_tap.hpp"

extern "C" {
#include <satlink/spec_tap.h>
}

namespace satlink::hal {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::IsNull;
using ::testing::Return;

class MockCharDeviceIo : public CharDeviceIo
{
  public:
    MOCK_METHOD(int, Ioctl, (unsigned long request, void *arg), (override));
};

TEST(SpecTapTest, GetVersionReadsMajorMinor)
{
    MockCharDeviceIo io;
    EXPECT_CALL(io, Ioctl(SATLINK_ST_IOC_GET_VERSION, _))
        .WillOnce(Invoke([](unsigned long /*request*/, void *arg) {
            auto *version = static_cast<satlink_st_version *>(arg);
            version->major = 1;
            version->minor = 3;
            return 0;
        }));

    const SpecTap tap(io);
    const SpecTapVersion version = tap.GetVersion();

    EXPECT_EQ(1U, version.major);
    EXPECT_EQ(3U, version.minor);
}

TEST(SpecTapTest, GetCtrlTranslatesFlags)
{
    MockCharDeviceIo io;
    EXPECT_CALL(io, Ioctl(SATLINK_ST_IOC_GET_CTRL, _))
        .WillOnce(Invoke([](unsigned long /*request*/, void *arg) {
            auto *ctrl = static_cast<satlink_st_ctrl *>(arg);
            ctrl->enable = 0;
            ctrl->window_en = 1;
            return 0;
        }));

    const SpecTap tap(io);
    const SpecTapCtrl ctrl = tap.GetCtrl();

    EXPECT_FALSE(ctrl.enable);
    EXPECT_TRUE(ctrl.window_en);
}

TEST(SpecTapTest, SetCtrlPacksFlagsIntoRequest)
{
    MockCharDeviceIo io;
    satlink_st_ctrl captured{};
    EXPECT_CALL(io, Ioctl(SATLINK_ST_IOC_SET_CTRL, _))
        .WillOnce(Invoke([&captured](unsigned long /*request*/, void *arg) {
            captured = *static_cast<const satlink_st_ctrl *>(arg);
            return 0;
        }));

    const SpecTap tap(io);
    tap.SetCtrl(SpecTapCtrl{.enable = true, .window_en = false});

    EXPECT_EQ(1U, captured.enable);
    EXPECT_EQ(0U, captured.window_en);
}

TEST(SpecTapTest, GetStatusTranslatesFlagsAndCount)
{
    MockCharDeviceIo io;
    EXPECT_CALL(io, Ioctl(SATLINK_ST_IOC_GET_STATUS, _))
        .WillOnce(Invoke([](unsigned long /*request*/, void *arg) {
            auto *status = static_cast<satlink_st_status *>(arg);
            status->busy = 0;
            status->frame_dropped = 1;
            status->frame_cnt = 4096;
            return 0;
        }));

    const SpecTap tap(io);
    const SpecTapStatus status = tap.GetStatus();

    EXPECT_FALSE(status.busy);
    EXPECT_TRUE(status.frame_dropped);
    EXPECT_EQ(4096U, status.frame_cnt);
}

TEST(SpecTapTest, ClearFrameDroppedIssuesArgumentlessIoctl)
{
    MockCharDeviceIo io;
    EXPECT_CALL(io, Ioctl(SATLINK_ST_IOC_CLEAR_FRAME_DROP, IsNull())).WillOnce(Return(0));

    const SpecTap tap(io);
    tap.ClearFrameDropped();
}

TEST(SpecTapTest, DriverErrorBecomesSystemError)
{
    MockCharDeviceIo io;
    EXPECT_CALL(io, Ioctl(SATLINK_ST_IOC_GET_STATUS, _)).WillOnce(Return(-EIO));

    const SpecTap tap(io);
    EXPECT_THROW({ (void)tap.GetStatus(); }, std::system_error);
}

} // namespace
} // namespace satlink::hal
