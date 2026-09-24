/**
 * @file test_diag.cpp
 * @brief satlink-diag command logic, run end to end through the HAL against fake devices.
 *
 * @verifies SRS-DIAG-001
 * @verifies SRS-HAL-001
 * @verifies SRS-PER-001
 * @verifies SRS-HKC-005
 */
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "satlink/boot/can_boot.h"
#include "satlink/diag/device_factory.hpp"
#include "satlink/diag/diag.hpp"
#include "satlink/hal/char_device_io.hpp"
#include "satlink/hal/i2c_bus.hpp"
#include "satlink/hal/ssm2603.hpp"

#include "fake_hkc_node.hpp"

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

class ForwardingCan final : public hal::CanPort
{
  public:
    explicit ForwardingCan(hal::CanPort &target) : target_(target) {}
    void Send(const hal::CanFrame &frame) override
    {
        target_.Send(frame);
    }
    std::optional<hal::CanFrame> Receive(std::chrono::milliseconds timeout) override
    {
        return target_.Receive(timeout);
    }

  private:
    hal::CanPort &target_;
};

/// Hands out the fakes by path and remembers what was opened.
class FakeFactory final : public DeviceFactory
{
  public:
    explicit FakeFactory(
        hal::test::FakeHkcNode::Mode node_mode = hal::test::FakeHkcNode::Mode::kBootloader,
        std::uint32_t node_max_image = 4096U)
        : node(node_mode, node_max_image)
    {
        char_devices[kFrameAccelPath] = &frame_accel;
        char_devices[kSpecTapPath] = &spec_tap;
        can_interfaces[kHkcCanInterface] = &node;
    }

    std::unique_ptr<hal::CanPort> OpenCan(const std::string &interface,
                                          const std::vector<std::uint32_t> &accept_ids) override
    {
        opened_can.emplace_back(interface, accept_ids);
        const auto found = can_interfaces.find(interface);
        if (found == can_interfaces.end())
        {
            throw std::system_error(ENODEV, std::generic_category(), "CAN interface " + interface);
        }
        return std::make_unique<ForwardingCan>(*found->second);
    }

    std::vector<std::uint8_t> ReadFile(const std::string &path) override
    {
        const auto found = files.find(path);
        if (found == files.end())
        {
            throw std::system_error(ENOENT, std::generic_category(), "open(" + path + ")");
        }
        return found->second;
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

    hal::test::FakeHkcNode node;
    std::map<std::string, hal::CanPort *> can_interfaces;
    std::vector<std::pair<std::string, std::vector<std::uint32_t>>> opened_can;
    std::map<std::string, std::vector<std::uint8_t>> files;
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

// ---- hkc: the housekeeping controller over CAN --------------------------------------------

using hal::test::FakeHkcNode;

/// Produced by tools/hkc/mkapp.py (see test_app_header.c).
std::vector<std::uint8_t> GoldenApp()
{
    return {0x53, 0x4C, 0x41, 0x50, 0x01, 0x00, 0x10, 0x00, 0x1F, 0x00, 0x00,
            0x00, 0x4C, 0x6C, 0x00, 0x00, 0x53, 0x61, 0x74, 0x4C, 0x69, 0x6E,
            0x6B, 0x20, 0x68, 0x6B, 0x63, 0x20, 0x61, 0x70, 0x70};
}

TEST(DiagTest, HkcPingShowsTheBootloaderState)
{
    FakeFactory factory;
    const Result result = RunCommand(factory, {"hkc", "ping"});
    EXPECT_EQ(kExitOk, result.code);
    EXPECT_EQ("bootloader: state=idle protocol=1 max_image=4096\n", result.out);
    ASSERT_EQ(1U, factory.opened_can.size());
    EXPECT_EQ(kHkcCanInterface, factory.opened_can[0].first);
    EXPECT_EQ((std::vector<std::uint32_t>{SATLINK_CANBOOT_ID_RSP}), factory.opened_can[0].second);
}

TEST(DiagTest, HkcPingOfARunningApplicationTimesOut)
{
    FakeFactory factory(FakeHkcNode::Mode::kApplication);
    const Result result = RunCommand(factory, {"hkc", "ping"});
    EXPECT_EQ(kExitTimeout, result.code);
    EXPECT_NE(std::string::npos, result.out.find("hkc enter"));
}

TEST(DiagTest, HkcEnterBringsUpTheBootloader)
{
    FakeFactory factory(FakeHkcNode::Mode::kApplication);
    const Result result = RunCommand(factory, {"hkc", "enter"});
    EXPECT_EQ(kExitOk, result.code);
    EXPECT_EQ("bootloader ready (protocol 1)\n", result.out);
    EXPECT_EQ(FakeHkcNode::Mode::kBootloader, factory.node.mode());
}

TEST(DiagTest, HkcEnterWithNobodyThereTimesOut)
{
    FakeFactory factory(FakeHkcNode::Mode::kDead);
    const Result result = RunCommand(factory, {"hkc", "enter"});
    EXPECT_EQ(kExitTimeout, result.code);
    EXPECT_NE(std::string::npos, result.out.find("no answer"));
}

TEST(DiagTest, HkcUploadRestartsTheApplicationTransfersTheImageAndStartsIt)
{
    FakeFactory factory(FakeHkcNode::Mode::kApplication);
    factory.files["app.img"] = GoldenApp();

    const Result result = RunCommand(factory, {"hkc", "upload", "app.img"});

    EXPECT_EQ(kExitOk, result.code) << result.err;
    EXPECT_NE(std::string::npos, result.out.find("bootloader ready (protocol 1)"));
    EXPECT_NE(std::string::npos, result.out.find("uploading 31 bytes"));
    EXPECT_NE(std::string::npos, result.out.find("  0%\n"));
    EXPECT_NE(std::string::npos, result.out.find("  100%\n"));
    EXPECT_NE(std::string::npos, result.out.find("image verified and started\n"));
    EXPECT_EQ(GoldenApp(), factory.node.Received());
    EXPECT_TRUE(satlink_canboot_rx_boot_requested(&factory.node.receiver()));
}

TEST(DiagTest, HkcUploadWithBootOffOnlyVerifiesTheImage)
{
    FakeFactory factory;
    factory.files["app.img"] = GoldenApp();

    const Result result = RunCommand(factory, {"hkc", "upload", "app.img", "boot=off"});

    EXPECT_EQ(kExitOk, result.code) << result.err;
    EXPECT_NE(std::string::npos, result.out.find("image verified\n"));
    EXPECT_FALSE(satlink_canboot_rx_boot_requested(&factory.node.receiver()));
}

TEST(DiagTest, HkcUploadReportsAMissingFileWithoutTouchingTheBus)
{
    FakeFactory factory;
    const Result result = RunCommand(factory, {"hkc", "upload", "missing.img"});
    EXPECT_EQ(kExitRuntimeError, result.code);
    EXPECT_NE(std::string::npos, result.err.find("missing.img"));
    EXPECT_TRUE(factory.opened_can.empty());
}

TEST(DiagTest, HkcUploadRefusesAnEmptyFile)
{
    FakeFactory factory;
    factory.files["empty.img"] = {};
    const Result result = RunCommand(factory, {"hkc", "upload", "empty.img"});
    EXPECT_EQ(kExitRuntimeError, result.code);
    EXPECT_NE(std::string::npos, result.err.find("is empty"));
    EXPECT_TRUE(factory.opened_can.empty());
}

TEST(DiagTest, HkcUploadChecksTheImageAgainstTheNodesCapacity)
{
    FakeFactory factory(FakeHkcNode::Mode::kBootloader, 16U);
    factory.files["app.img"] = GoldenApp();
    const Result result = RunCommand(factory, {"hkc", "upload", "app.img"});
    EXPECT_EQ(kExitRuntimeError, result.code);
    EXPECT_NE(std::string::npos, result.err.find("accepts up to 16"));
}

TEST(DiagTest, HkcUploadToADeadNodeTimesOut)
{
    FakeFactory factory(FakeHkcNode::Mode::kDead);
    factory.files["app.img"] = GoldenApp();
    EXPECT_EQ(kExitTimeout, RunCommand(factory, {"hkc", "upload", "app.img"}).code);
}

TEST(DiagTest, HkcUploadReportsACorruptedTransfer)
{
    FakeFactory factory;
    factory.node.corrupt_data_frame = 2;
    factory.files["app.img"] = GoldenApp();
    const Result result = RunCommand(factory, {"hkc", "upload", "app.img"});
    EXPECT_EQ(kExitRuntimeError, result.code);
    EXPECT_NE(std::string::npos, result.err.find("CRC"));
}

TEST(DiagTest, HkcTelemetryPrintsTheSnapshot)
{
    FakeFactory factory(FakeHkcNode::Mode::kApplication);
    satlink_hk_snapshot_t snapshot{};
    snapshot.temp_mdegc = -500;
    snapshot.vccint_mv = 1002;
    snapshot.vccaux_mv = 1801;
    snapshot.vccbram_mv = 999;
    snapshot.temp_level = SATLINK_HK_ALARM;
    snapshot.uptime_s = 77;
    factory.node.BroadcastTelemetry(snapshot, 3);

    const Result result = RunCommand(factory, {"hkc", "telemetry"});

    EXPECT_EQ(kExitOk, result.code);
    EXPECT_EQ("temperature=-0.500 C (alarm)\n"
              "vccint=1002 mV (ok)\n"
              "vccaux=1801 mV (ok)\n"
              "vccbram=999 mV (ok)\n"
              "uptime=77 s watchdog_reset=no\n",
              result.out);
    ASSERT_EQ(1U, factory.opened_can.size());
    EXPECT_EQ((std::vector<std::uint32_t>{0x100U, 0x101U, 0x102U}), factory.opened_can[0].second);
}

TEST(DiagTest, HkcTelemetryTimesOutWhenTheApplicationIsSilent)
{
    FakeFactory factory(FakeHkcNode::Mode::kApplication);
    const Result result = RunCommand(factory, {"hkc", "telemetry", "timeout_ms=500"});
    EXPECT_EQ(kExitTimeout, result.code);
    EXPECT_NE(std::string::npos, result.out.find("no telemetry within 500 ms"));
}

TEST(DiagTest, HkcDevOptionSelectsTheCanInterface)
{
    FakeFactory factory;
    factory.can_interfaces["can1"] = &factory.node;
    EXPECT_EQ(kExitOk, RunCommand(factory, {"hkc", "ping", "--dev", "can1"}).code);
    ASSERT_EQ(1U, factory.opened_can.size());
    EXPECT_EQ("can1", factory.opened_can[0].first);

    const Result missing = RunCommand(factory, {"hkc", "ping", "--dev", "can9"});
    EXPECT_EQ(kExitRuntimeError, missing.code);
    EXPECT_NE(std::string::npos, missing.err.find("can9"));
}

TEST(DiagTest, HkcUsageErrors)
{
    FakeFactory factory;
    EXPECT_EQ(kExitUsageError, RunCommand(factory, {"hkc"}).code);
    EXPECT_EQ(kExitUsageError, RunCommand(factory, {"hkc", "reset"}).code);
    EXPECT_EQ(kExitUsageError, RunCommand(factory, {"hkc", "upload"}).code);
    EXPECT_EQ(kExitUsageError, RunCommand(factory, {"hkc", "upload", "a.img", "boot=maybe"}).code);
    EXPECT_EQ(kExitUsageError, RunCommand(factory, {"hkc", "telemetry", "timeout_ms=x"}).code);
    EXPECT_EQ(kExitUsageError, RunCommand(factory, {"hkc", "ping", "extra"}).code);
    EXPECT_TRUE(factory.opened_can.empty());
}

} // namespace
} // namespace satlink::diag
