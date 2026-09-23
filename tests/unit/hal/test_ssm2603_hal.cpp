/**
 * @file test_ssm2603_hal.cpp
 * @brief Ssm2603 register sequences, checked byte for byte against a fake I2C bus.
 *
 * @verifies SRS-PER-001
 * @verifies SRS-HAL-002
 */
#include <cerrno>
#include <cstdint>
#include <gtest/gtest.h>
#include <span>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "satlink/hal/i2c_bus.hpp"
#include "satlink/hal/ssm2603.hpp"

namespace satlink::hal {
namespace {

using Frame = std::vector<std::uint8_t>;

/// Records every write; can be told to fail.
class RecordingBus : public I2cBus
{
  public:
    int Write(std::span<const std::uint8_t> data) override
    {
        if (fail_with != 0)
        {
            return fail_with;
        }
        frames.emplace_back(data.begin(), data.end());
        return 0;
    }

    std::vector<Frame> frames;
    int fail_with = 0;
};

TEST(Ssm2603Test, InitializeSendsTheFullSequenceInOrder)
{
    RecordingBus bus;
    Ssm2603 codec(bus);

    codec.Initialize();

    const std::vector<Frame> expected = {
        {0x1E, 0x00}, // R15 reset
        {0x0C, 0x72}, // R6: mic, output, oscillator and clock output off while configuring
        {0x00, 0x17}, // R0: left line-in 0 dB
        {0x02, 0x17}, // R1: right line-in 0 dB
        {0x04, 0x79}, // R2: left headphone 0 dB
        {0x06, 0x79}, // R3: right headphone 0 dB
        {0x08, 0x12}, // R4: DAC to output, mic muted, line input selected
        {0x0A, 0x00}, // R5: DAC unmuted
        {0x0E, 0x0A}, // R7: I2S, 24-bit, slave
        {0x10, 0x00}, // R8: normal mode, 48 kHz
        {0x12, 0x01}, // R9: active
        {0x0C, 0x62}, // R6: output stage on
    };
    EXPECT_EQ(expected, bus.frames);
    EXPECT_TRUE(codec.IsInitialized());
}

TEST(Ssm2603Test, WordLengthGoesIntoTheInterfaceRegister)
{
    RecordingBus bus16;
    Ssm2603 codec16(bus16);
    codec16.Initialize(Ssm2603Config{.word_length = Ssm2603WordLength::k16Bit});
    EXPECT_EQ((Frame{0x0E, 0x02}), bus16.frames[8]);

    RecordingBus bus32;
    Ssm2603 codec32(bus32);
    codec32.Initialize(Ssm2603Config{.word_length = Ssm2603WordLength::k32Bit});
    EXPECT_EQ((Frame{0x0E, 0x0E}), bus32.frames[8]);
}

TEST(Ssm2603Test, ShadowHoldsWhatWasWritten)
{
    RecordingBus bus;
    Ssm2603 codec(bus);
    codec.Initialize();

    EXPECT_EQ(0x062U, codec.ShadowRegister(Ssm2603::kRegPower));
    EXPECT_EQ(0x001U, codec.ShadowRegister(Ssm2603::kRegActive));
    EXPECT_EQ(0x00AU, codec.ShadowRegister(Ssm2603::kRegDigitalInterface));
    EXPECT_EQ(0x079U, codec.ShadowRegister(Ssm2603::kRegLeftHeadphone));
    EXPECT_THROW({ (void)codec.ShadowRegister(0x10); }, std::out_of_range);
}

TEST(Ssm2603Test, HeadphoneVolumeUsesTheBothChannelsBitAcrossTheNinthDataBit)
{
    RecordingBus bus;
    Ssm2603 codec(bus);
    codec.Initialize();
    bus.frames.clear();

    codec.SetHeadphoneVolume(0x30);

    // Register 2 with data 0x130: address bits 0000010, data bit 8 = 1 in the low bit of byte 0.
    EXPECT_EQ((std::vector<Frame>{{0x05, 0x30}}), bus.frames);
    EXPECT_EQ(0x130U, codec.ShadowRegister(Ssm2603::kRegLeftHeadphone));
    EXPECT_EQ(0x030U, codec.ShadowRegister(Ssm2603::kRegRightHeadphone));
}

TEST(Ssm2603Test, VolumeAboveSevenBitsIsRejectedWithoutTraffic)
{
    RecordingBus bus;
    Ssm2603 codec(bus);
    codec.Initialize();
    bus.frames.clear();

    EXPECT_THROW(codec.SetHeadphoneVolume(0x80), std::out_of_range);
    EXPECT_TRUE(bus.frames.empty());
}

TEST(Ssm2603Test, MuteSetsAndClearsOnlyTheDacMuteBit)
{
    RecordingBus bus;
    Ssm2603 codec(bus);
    codec.Initialize();
    bus.frames.clear();

    codec.MuteDac(true);
    codec.MuteDac(false);

    EXPECT_EQ((std::vector<Frame>{{0x0A, 0x08}, {0x0A, 0x00}}), bus.frames);
}

TEST(Ssm2603Test, ShutdownDeactivatesThenPowersDown)
{
    RecordingBus bus;
    Ssm2603 codec(bus);
    codec.Initialize();
    bus.frames.clear();

    codec.Shutdown();

    EXPECT_EQ((std::vector<Frame>{{0x12, 0x00}, {0x0C, 0xFF}}), bus.frames);
    EXPECT_FALSE(codec.IsInitialized());
}

TEST(Ssm2603Test, UseBeforeInitializeIsALogicErrorWithoutTraffic)
{
    RecordingBus bus;
    Ssm2603 codec(bus);

    EXPECT_THROW(codec.SetHeadphoneVolume(0x79), std::logic_error);
    EXPECT_THROW(codec.MuteDac(true), std::logic_error);
    EXPECT_THROW(codec.Shutdown(), std::logic_error);
    EXPECT_TRUE(bus.frames.empty());
}

TEST(Ssm2603Test, BusErrorBecomesSystemErrorAndLeavesTheCodecUninitialized)
{
    RecordingBus bus;
    bus.fail_with = -EIO;
    Ssm2603 codec(bus);

    EXPECT_THROW(codec.Initialize(), std::system_error);
    EXPECT_FALSE(codec.IsInitialized());
}

TEST(Ssm2603Test, AssumeConfiguredMatchesInitializeWithoutTouchingTheBus)
{
    RecordingBus real_bus;
    Ssm2603 initialized(real_bus);
    initialized.Initialize();

    RecordingBus idle_bus;
    Ssm2603 assumed(idle_bus);
    assumed.AssumeConfigured();

    EXPECT_TRUE(idle_bus.frames.empty());
    EXPECT_TRUE(assumed.IsInitialized());
    for (std::uint8_t reg = 0; reg < 16; ++reg)
    {
        EXPECT_EQ(initialized.ShadowRegister(reg), assumed.ShadowRegister(reg))
            << "register " << int{reg};
    }

    assumed.SetHeadphoneVolume(0x79);
    EXPECT_EQ((std::vector<Frame>{{0x05, 0x79}}), idle_bus.frames);
}

} // namespace
} // namespace satlink::hal
