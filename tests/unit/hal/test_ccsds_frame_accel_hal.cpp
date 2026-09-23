/**
 * @file test_ccsds_frame_accel_hal.cpp
 * @brief FrameAccelerator logic against a mocked CharDeviceIo - no board required.
 *
 * @verifies SRS-HAL-001
 * @verifies SRS-HAL-002
 * @verifies SRS-DRV-002
 */
#include <cerrno>
#include <chrono>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>

#include "satlink/hal/ccsds_frame_accel.hpp"
#include "satlink/hal/char_device_io.hpp"

extern "C" {
#include <satlink/ccsds_frame_accel.h>
}

namespace satlink::hal {
namespace {

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

class MockCharDeviceIo : public CharDeviceIo
{
  public:
    MOCK_METHOD(int, Ioctl, (unsigned long request, void *arg), (override));
};

TEST(FrameAcceleratorTest, GetVersionReadsMajorMinor)
{
    MockCharDeviceIo io;
    EXPECT_CALL(io, Ioctl(SATLINK_FA_IOC_GET_VERSION, _))
        .WillOnce(Invoke([](unsigned long /*request*/, void *arg) {
            auto *version = static_cast<satlink_fa_version *>(arg);
            version->major = 2;
            version->minor = 1;
            return 0;
        }));

    const FrameAccelerator fa(io);
    const FrameAccelVersion version = fa.GetVersion();

    EXPECT_EQ(2U, version.major);
    EXPECT_EQ(1U, version.minor);
}

TEST(FrameAcceleratorTest, GetCtrlTranslatesFlags)
{
    MockCharDeviceIo io;
    EXPECT_CALL(io, Ioctl(SATLINK_FA_IOC_GET_CTRL, _))
        .WillOnce(Invoke([](unsigned long /*request*/, void *arg) {
            auto *ctrl = static_cast<satlink_fa_ctrl *>(arg);
            ctrl->randomizer_en = 1;
            ctrl->irq_en = 0;
            return 0;
        }));

    const FrameAccelerator fa(io);
    const FrameAccelCtrl ctrl = fa.GetCtrl();

    EXPECT_TRUE(ctrl.randomizer_en);
    EXPECT_FALSE(ctrl.irq_en);
}

TEST(FrameAcceleratorTest, SetCtrlPacksFlagsIntoRequest)
{
    MockCharDeviceIo io;
    satlink_fa_ctrl captured{};
    EXPECT_CALL(io, Ioctl(SATLINK_FA_IOC_SET_CTRL, _))
        .WillOnce(Invoke([&captured](unsigned long /*request*/, void *arg) {
            captured = *static_cast<const satlink_fa_ctrl *>(arg);
            return 0;
        }));

    const FrameAccelerator fa(io);
    fa.SetCtrl(FrameAccelCtrl{.randomizer_en = true, .irq_en = true});

    EXPECT_EQ(1U, captured.randomizer_en);
    EXPECT_EQ(1U, captured.irq_en);
}

TEST(FrameAcceleratorTest, WaitFrameReturnsStatsOnSuccess)
{
    MockCharDeviceIo io;
    EXPECT_CALL(io, Ioctl(SATLINK_FA_IOC_WAIT_FRAME, _))
        .WillOnce(Invoke([](unsigned long /*request*/, void *arg) {
            auto *wait_frame = static_cast<satlink_fa_wait_frame *>(arg);
            wait_frame->timed_out = 0;
            wait_frame->stats.crc = 0;
            wait_frame->stats.last_len_bytes = 223;
            wait_frame->stats.frame_cnt = 42;
            return 0;
        }));

    const FrameAccelerator fa(io);
    const std::optional<FrameStats> stats = fa.WaitFrame(std::chrono::milliseconds(100));

    ASSERT_TRUE(stats.has_value());
    const FrameStats got = stats.value_or(FrameStats{});
    EXPECT_EQ(0U, got.crc);
    EXPECT_EQ(223U, got.last_len_bytes);
    EXPECT_EQ(42U, got.frame_cnt);
}

TEST(FrameAcceleratorTest, WaitFrameReturnsNulloptOnTimeout)
{
    MockCharDeviceIo io;
    EXPECT_CALL(io, Ioctl(SATLINK_FA_IOC_WAIT_FRAME, _))
        .WillOnce(Invoke([](unsigned long /*request*/, void *arg) {
            static_cast<satlink_fa_wait_frame *>(arg)->timed_out = 1;
            return 0;
        }));

    const FrameAccelerator fa(io);
    const std::optional<FrameStats> stats = fa.WaitFrame(std::chrono::milliseconds(5));

    EXPECT_FALSE(stats.has_value());
}

TEST(FrameAcceleratorTest, DriverErrorBecomesSystemError)
{
    MockCharDeviceIo io;
    EXPECT_CALL(io, Ioctl(SATLINK_FA_IOC_GET_VERSION, _)).WillOnce(Return(-EIO));

    const FrameAccelerator fa(io);
    EXPECT_THROW({ (void)fa.GetVersion(); }, std::system_error);
}

} // namespace
} // namespace satlink::hal
