/**
 * @file ssm2603.cpp
 * @implements SRS-PER-001
 */
#include "satlink/hal/ssm2603.hpp"

#include <array>
#include <stdexcept>

#include "hal_error.hpp"

namespace satlink::hal {
namespace {

// Register 6 (power management): a set bit powers the block down.
constexpr std::uint16_t kPdnMic = 0x002U;
constexpr std::uint16_t kPdnOut = 0x010U;
constexpr std::uint16_t kPdnOsc = 0x020U;
constexpr std::uint16_t kPdnClkOut = 0x040U;
constexpr std::uint16_t kPdnAll = 0x0FFU;

// Registers 2/3 (headphone volume): bit 8 updates both channels at once.
constexpr std::uint16_t kHeadphoneBoth = 0x100U;

// Register 4 (analog path): line input selected, microphone muted, DAC routed to the output.
constexpr std::uint16_t kDacSel = 0x010U;
constexpr std::uint16_t kMuteMic = 0x002U;

// Register 5 (digital path).
constexpr std::uint16_t kDacMute = 0x008U;

// Register 7 (digital audio interface): I2S format, word length in bits [3:2].
constexpr std::uint16_t kFormatI2s = 0x002U;

constexpr std::uint16_t kLineInZeroDb = 0x017U;
constexpr std::uint16_t kHeadphoneZeroDb = 0x079U;

std::array<std::uint8_t, 2> EncodeFrame(std::uint8_t reg, std::uint16_t value)
{
    // Seven register-address bits, then the ninth data bit, then the low eight data bits.
    const unsigned high =
        (static_cast<unsigned>(reg) << 1U) | ((static_cast<unsigned>(value) >> 8U) & 1U);
    return {static_cast<std::uint8_t>(high), static_cast<std::uint8_t>(value & 0xFFU)};
}

struct Step
{
    std::uint8_t reg;
    std::uint16_t value;
};

// The register writes that bring the codec to its fixed setup, in order.
std::array<Step, 12> InitSequence(const Ssm2603Config &config)
{
    const auto word_length = static_cast<unsigned>(config.word_length);
    return {{
        {Ssm2603::kRegReset, 0x000U},
        // Keep the output stage off while everything else is configured, to avoid a pop.
        {Ssm2603::kRegPower, static_cast<std::uint16_t>(kPdnMic | kPdnOut | kPdnOsc | kPdnClkOut)},
        {Ssm2603::kRegLeftLineIn, kLineInZeroDb},
        {Ssm2603::kRegRightLineIn, kLineInZeroDb},
        {Ssm2603::kRegLeftHeadphone, kHeadphoneZeroDb},
        {Ssm2603::kRegRightHeadphone, kHeadphoneZeroDb},
        {Ssm2603::kRegAnalogPath, static_cast<std::uint16_t>(kDacSel | kMuteMic)},
        {Ssm2603::kRegDigitalPath, 0x000U},
        {Ssm2603::kRegDigitalInterface,
         static_cast<std::uint16_t>(kFormatI2s | (word_length << 2U))},
        {Ssm2603::kRegSamplingRate, 0x000U},
        {Ssm2603::kRegActive, 0x001U},
        {Ssm2603::kRegPower, static_cast<std::uint16_t>(kPdnMic | kPdnOsc | kPdnClkOut)},
    }};
}

} // namespace

void Ssm2603::WriteRegister(std::uint8_t reg, std::uint16_t value)
{
    const std::array<std::uint8_t, 2> frame = EncodeFrame(reg, value);
    detail::ThrowOnFailure(bus_.Write(frame), "SSM2603 register write");
    if (static_cast<std::size_t>(reg) < kShadowSize)
    {
        shadow_[reg] = value;
    }
}

void Ssm2603::RequireInitialized() const
{
    if (!initialized_)
    {
        throw std::logic_error("Ssm2603 used before Initialize()");
    }
}

void Ssm2603::Initialize(const Ssm2603Config &config)
{
    for (const Step &step : InitSequence(config))
    {
        WriteRegister(step.reg, step.value);
    }
    initialized_ = true;
}

void Ssm2603::AssumeConfigured(const Ssm2603Config &config)
{
    shadow_ = {};
    for (const Step &step : InitSequence(config))
    {
        shadow_[step.reg] = step.value;
    }
    initialized_ = true;
}

void Ssm2603::Shutdown()
{
    RequireInitialized();
    WriteRegister(kRegActive, 0x000U);
    WriteRegister(kRegPower, kPdnAll);
    initialized_ = false;
}

void Ssm2603::SetHeadphoneVolume(std::uint8_t volume)
{
    RequireInitialized();
    if (volume > kHeadphoneVolumeMax)
    {
        throw std::out_of_range("headphone volume is a 7-bit code");
    }
    WriteRegister(kRegLeftHeadphone, static_cast<std::uint16_t>(kHeadphoneBoth | volume));
    // The "both channels" bit made the codec copy the value into the right-channel register too.
    shadow_[kRegRightHeadphone] = volume;
}

void Ssm2603::MuteDac(bool mute)
{
    RequireInitialized();
    const std::uint16_t current = shadow_[kRegDigitalPath];
    const std::uint16_t next = mute ? static_cast<std::uint16_t>(current | kDacMute)
                                    : static_cast<std::uint16_t>(current & ~kDacMute);
    WriteRegister(kRegDigitalPath, next);
}

std::uint16_t Ssm2603::ShadowRegister(std::uint8_t reg) const
{
    if (static_cast<std::size_t>(reg) >= kShadowSize)
    {
        throw std::out_of_range("SSM2603 register number above 0x0F");
    }
    return shadow_[reg];
}

} // namespace satlink::hal
