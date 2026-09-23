/**
 * @file test_diag.cpp
 * @brief satlink-diag command logic, run end to end through the HAL against fake devices.
 *
 * @verifies SRS-DIAG-001
 * @verifies SRS-HAL-001
 * @verifies SRS-PER-001
 */
#include <cerrno>
#include <cstdint>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "satlink/diag/device_factory.hpp"
#include "satlink/diag/diag.hpp"
#include "satlink/hal/char_device_io.hpp"
#include "satlink/hal/i2c_bus.hpp"
#include "satlink/hal/ssm2603.hpp"

extern "C" {
#include <satlink/ccsds_frame_accel.h>
#include <satlink/spec_tap.h>
}

namespace satlink::diag {
namespace {

using Frame = std::vector<std::uint8_t>;

/// Behaves like the ccsds_frame_accel driver, with state.
class FakeFrameAccelDriver final : public hal::CharDeviceIo
{
  public:
    int Ioctl(unsigned long request, void *arg) override
    {
        if (request == SATLINK_FA_IOC_GET_VERSION)
        {
            auto *version = static_cast<satlink_fa_version *>(arg);
            version->major = 1;
            version->minor = 2;
            return 0;
        }
        if (request == SATLINK_FA_IOC_GET_CTRL)
        {
            *static_cast<satlink_fa_ctrl *>(arg) = ctrl;
            return 0;
        }
        if (request == SATLINK_FA_IOC_SET_CTRL)
        {
            ctrl = *static_cast<const satlink_fa_ctrl *>(arg);
            return 0;
        }
        if (request == SATLINK_FA_IOC_WAIT_FRAME)
        {
            auto *wait = static_cast<satlink_fa_wait_frame *>(arg);
            last_timeout_ms = wait->timeout_ms;
            wait->timed_out = frame_ready ? 0U : 1U;
            wait->stats = stats;
            return 0;
        }
        return -ENOTTY;
    }

    satlink_fa_ctrl ctrl{};
    bool frame_ready = true;
    satlink_fa_frame_stats stats{.crc = 0, .reserved = 0, .last_len_bytes = 223, .frame_cnt = 42};
    std::uint32_t last_timeout_ms = 0;
};

/// Behaves like the spec_tap driver, with state.
class FakeSpecTapDriver final : public hal::CharDeviceIo
{
  public:
    int Ioctl(unsigned long request, void *arg) override
    {
        if (request == SATLINK_ST_IOC_GET_VERSION)
        {
            auto *version = static_cast<satlink_st_version *>(arg);
            version->major = 1;
            version->minor = 0;
            return 0;
        }
        if (request == SATLINK_ST_IOC_GET_CTRL)
        {
            *static_cast<satlink_st_ctrl *>(arg) = ctrl;
            return 0;
        }
        if (request == SATLINK_ST_IOC_SET_CTRL)
        {
            ctrl = *static_cast<const satlink_st_ctrl *>(arg);
            return 0;
        }
        if (request == SATLINK_ST_IOC_GET_STATUS)
        {
            *static_cast<satlink_st_status *>(arg) = status;
            return 0;
        }
        if (request == SATLINK_ST_IOC_CLEAR_FRAME_DROP)
        {
            status.frame_dropped = 0;
            return 0;
        }
        return -ENOTTY;
    }

    satlink_st_ctrl ctrl{};
    satlink_st_status status{.busy = 0, .frame_dropped = 1, .reserved = {}, .frame_cnt = 7};
};

class RecordingBus final : public hal::I2cBus
{
  public:
    int Write(std::span<const std::uint8_t> data) override
    {
        frames.emplace_back(data.begin(), data.end());
        return 0;
    }

    std::vector<Frame> frames;
};

class ForwardingIo final : public hal::CharDeviceIo
{
  public:
    explicit ForwardingIo(hal::CharDeviceIo &target) : target_(target) {}
    int Ioctl(unsigned long request, void *arg) override
    {
        return target_.Ioctl(request, arg);
    }

  private:
    hal::CharDeviceIo &target_;
};

class ForwardingBus final : public hal::I2cBus
{
  public:
    explicit ForwardingBus(hal::I2cBus &target) : target_(target) {}
    int Write(std::span<const std::uint8_t> data) override
    {
        return target_.Write(data);
    }

  private:
    hal::I2cBus &target_;
};

/// Hands out the fakes by path and remembers what was opened.
class FakeFactory final : public DeviceFactory
{
  public:
    FakeFactory()
    {
        char_devices[kFrameAccelPath] = &frame_accel;
        char_devices[kSpecTapPath] = &spec_tap;
    }

    std::unique_ptr<hal::CharDeviceIo> OpenCharDevice(const std::string &path) override
    {
        opened_char.push_back(path);
        const auto found = char_devices.find(path);
        if (found == char_devices.end())
        {
            throw std::system_error(ENOENT, std::generic_category(), "open(" + path + ")");
        }
        return std::make_unique<ForwardingIo>(*found->second);
    }

    std::unique_ptr<hal::I2cBus> OpenI2c(const std::string &path, std::uint8_t address) override
    {
        opened_i2c.emplace_back(path, address);
        if (path != kCodecBusPath)
        {
            throw std::system_error(ENOENT, std::generic_category(), "open(" + path + ")");
        }
        return std::make_unique<ForwardingBus>(codec_bus);
    }

    FakeFrameAccelDriver frame_accel;
    FakeSpecTapDriver spec_tap;
    RecordingBus codec_bus;
    std::map<std::string, hal::CharDeviceIo *> char_devices;
    std::vector<std::string> opened_char;
    std::vector<std::pair<std::string, std::uint8_t>> opened_i2c;
};

struct Result
{
    int code;
    std::string out;
    std::string err;
};

Result RunCommand(FakeFactory &factory, const std::vector<std::string> &args)
{
    std::ostringstream out;
    std::ostringstream err;
    const int code = RunDiag(args, factory, out, err);
    return Result{code, out.str(), err.str()};
}

TEST(DiagTest, NoArgumentsPrintsUsageAndFails)
{
    FakeFactory factory;
    const Result result = RunCommand(factory, {});
    EXPECT_EQ(kExitUsageError, result.code);
    EXPECT_NE(std::string::npos, result.err.find("usage: satlink-diag"));
}

TEST(DiagTest, HelpPrintsUsageToStdout)
{
    FakeFactory factory;
    const Result result = RunCommand(factory, {"help"});
    EXPECT_EQ(kExitOk, result.code);
    EXPECT_NE(std::string::npos, result.out.find("usage: satlink-diag"));
}

TEST(DiagTest, UnknownBlockIsAUsageErrorAndOpensNothing)
{
    FakeFactory factory;
    const Result result = RunCommand(factory, {"radio", "version"});
    EXPECT_EQ(kExitUsageError, result.code);
    EXPECT_NE(std::string::npos, result.err.find("unknown block 'radio'"));
    EXPECT_TRUE(factory.opened_char.empty());
}

TEST(DiagTest, BlockWithoutCommandIsAUsageError)
{
    FakeFactory factory;
    EXPECT_EQ(kExitUsageError, RunCommand(factory, {"fa"}).code);
}

TEST(DiagTest, FrameAccelVersion)
{
    FakeFactory factory;
    const Result result = RunCommand(factory, {"fa", "version"});
    EXPECT_EQ(kExitOk, result.code);
    EXPECT_EQ("ccsds_frame_accel version 1.2\n", result.out);
    ASSERT_EQ(1U, factory.opened_char.size());
    EXPECT_EQ(kFrameAccelPath, factory.opened_char[0]);
}

TEST(DiagTest, DevOptionOverridesTheDeviceNode)
{
    FakeFactory factory;
    factory.char_devices["/dev/fa-alt"] = &factory.frame_accel;
    const Result result = RunCommand(factory, {"fa", "version", "--dev", "/dev/fa-alt"});
    EXPECT_EQ(kExitOk, result.code);
    ASSERT_EQ(1U, factory.opened_char.size());
    EXPECT_EQ("/dev/fa-alt", factory.opened_char[0]);
}

TEST(DiagTest, FrameAccelCtrlShowsThenChangesOnlyTheGivenBits)
{
    FakeFactory factory;
    EXPECT_EQ("randomizer=off irq=off\n", RunCommand(factory, {"fa", "ctrl"}).out);

    factory.frame_accel.ctrl.irq_en = 1;
    const Result result = RunCommand(factory, {"fa", "ctrl", "randomizer=on"});
    EXPECT_EQ(kExitOk, result.code);
    EXPECT_EQ("randomizer=on irq=on\n", result.out);
    EXPECT_EQ(1U, factory.frame_accel.ctrl.randomizer_en);
    EXPECT_EQ(1U, factory.frame_accel.ctrl.irq_en);
}

TEST(DiagTest, BadOnOffValueIsRejectedBeforeAnyDeviceIsOpened)
{
    FakeFactory factory;
    const Result result = RunCommand(factory, {"fa", "ctrl", "randomizer=maybe"});
    EXPECT_EQ(kExitUsageError, result.code);
    EXPECT_NE(std::string::npos, result.err.find("randomizer must be on or off"));
    EXPECT_TRUE(factory.opened_char.empty());
}

TEST(DiagTest, UnknownOptionAndStrayArgumentAreRejected)
{
    FakeFactory factory;
    EXPECT_EQ(kExitUsageError, RunCommand(factory, {"fa", "ctrl", "speed=fast"}).code);
    EXPECT_EQ(kExitUsageError, RunCommand(factory, {"fa", "version", "extra"}).code);
    EXPECT_EQ(kExitUsageError, RunCommand(factory, {"fa", "ctrl", "irq=on", "irq=off"}).code);
}

TEST(DiagTest, FrameAccelWaitPrintsTheFrame)
{
    FakeFactory factory;
    const Result result = RunCommand(factory, {"fa", "wait", "timeout_ms=50"});
    EXPECT_EQ(kExitOk, result.code);
    EXPECT_EQ("frame: crc=0x0000 length=223 count=42\n", result.out);
    EXPECT_EQ(50U, factory.frame_accel.last_timeout_ms);
}

TEST(DiagTest, FrameAccelWaitDefaultsToOneSecond)
{
    FakeFactory factory;
    EXPECT_EQ(kExitOk, RunCommand(factory, {"fa", "wait"}).code);
    EXPECT_EQ(1000U, factory.frame_accel.last_timeout_ms);
}

TEST(DiagTest, FrameAccelWaitTimeoutHasItsOwnExitCode)
{
    FakeFactory factory;
    factory.frame_accel.frame_ready = false;
    const Result result = RunCommand(factory, {"fa", "wait", "timeout_ms=20"});
    EXPECT_EQ(kExitTimeout, result.code);
    EXPECT_EQ("timeout after 20 ms\n", result.out);
}

TEST(DiagTest, NonNumericTimeoutIsAUsageError)
{
    FakeFactory factory;
    EXPECT_EQ(kExitUsageError, RunCommand(factory, {"fa", "wait", "timeout_ms=soon"}).code);
}

TEST(DiagTest, MissingDeviceIsARuntimeError)
{
    FakeFactory factory;
    factory.char_devices.clear();
    const Result result = RunCommand(factory, {"fa", "version"});
    EXPECT_EQ(kExitRuntimeError, result.code);
    EXPECT_NE(std::string::npos, result.err.find("error: open("));
}

TEST(DiagTest, SpecTapVersionStatusAndClear)
{
    FakeFactory factory;
    EXPECT_EQ("spec_tap version 1.0\n", RunCommand(factory, {"spec", "version"}).out);
    EXPECT_EQ("busy=off dropped=on frames=7\n", RunCommand(factory, {"spec", "status"}).out);

    const Result cleared = RunCommand(factory, {"spec", "clear"});
    EXPECT_EQ(kExitOk, cleared.code);
    EXPECT_EQ("dropped-frame flag cleared\n", cleared.out);
    EXPECT_EQ("busy=off dropped=off frames=7\n", RunCommand(factory, {"spec", "status"}).out);
}

TEST(DiagTest, SpecTapCtrlChangesOnlyTheGivenBits)
{
    FakeFactory factory;
    const Result result = RunCommand(factory, {"spec", "ctrl", "enable=on", "window=off"});
    EXPECT_EQ(kExitOk, result.code);
    EXPECT_EQ("enable=on window=off\n", result.out);
    EXPECT_EQ(1U, factory.spec_tap.ctrl.enable);
    EXPECT_EQ(0U, factory.spec_tap.ctrl.window_en);
}

TEST(DiagTest, CodecInitRunsTheFullSequenceOnTheCodecAddress)
{
    FakeFactory factory;
    const Result result = RunCommand(factory, {"codec", "init"});
    EXPECT_EQ(kExitOk, result.code);
    EXPECT_EQ("codec initialised\n", result.out);
    EXPECT_EQ(12U, factory.codec_bus.frames.size());
    ASSERT_EQ(1U, factory.opened_i2c.size());
    EXPECT_EQ(kCodecBusPath, factory.opened_i2c[0].first);
    EXPECT_EQ(hal::Ssm2603::kI2cAddress, factory.opened_i2c[0].second);
}

TEST(DiagTest, CodecInitHonoursTheWordLength)
{
    FakeFactory factory;
    EXPECT_EQ(kExitOk, RunCommand(factory, {"codec", "init", "wordlength=16"}).code);
    EXPECT_EQ((Frame{0x0E, 0x02}), factory.codec_bus.frames[8]);
    EXPECT_EQ(kExitUsageError, RunCommand(factory, {"codec", "init", "wordlength=18"}).code);
}

TEST(DiagTest, CodecVolumeWritesOnlyTheVolumeRegister)
{
    FakeFactory factory;
    const Result result = RunCommand(factory, {"codec", "volume", "121"});
    EXPECT_EQ(kExitOk, result.code);
    EXPECT_EQ("headphone volume set to 121\n", result.out);
    EXPECT_EQ((std::vector<Frame>{{0x05, 0x79}}), factory.codec_bus.frames);
}

TEST(DiagTest, CodecVolumeOutOfRangeOpensNothing)
{
    FakeFactory factory;
    const Result result = RunCommand(factory, {"codec", "volume", "200"});
    EXPECT_EQ(kExitUsageError, result.code);
    EXPECT_TRUE(factory.opened_i2c.empty());
}

TEST(DiagTest, CodecMuteWritesOnlyTheDigitalPathRegister)
{
    FakeFactory factory;
    const Result result = RunCommand(factory, {"codec", "mute", "on"});
    EXPECT_EQ(kExitOk, result.code);
    EXPECT_EQ("DAC muted\n", result.out);
    EXPECT_EQ((std::vector<Frame>{{0x0A, 0x08}}), factory.codec_bus.frames);
    EXPECT_EQ(kExitUsageError, RunCommand(factory, {"codec", "mute"}).code);
}

} // namespace
} // namespace satlink::diag
