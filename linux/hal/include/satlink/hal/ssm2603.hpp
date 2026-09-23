/**
 * @file ssm2603.hpp
 * @brief C++20 HAL for the SSM2603 audio codec, configured over I2C from Linux.
 *
 * The codec's audio data path belongs to the PL modem (I2S); Linux only sets the codec up. The
 * part cannot be read back, so this class keeps a shadow of every register it has written.
 *
 * @implements SRS-PER-001
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "satlink/hal/i2c_bus.hpp"

namespace satlink::hal {

/// Word length of the I2S data, register 7 bits [3:2].
enum class Ssm2603WordLength : std::uint8_t
{
    k16Bit = 0,
    k20Bit = 1,
    k24Bit = 2,
    k32Bit = 3,
};

struct Ssm2603Config
{
    Ssm2603WordLength word_length = Ssm2603WordLength::k24Bit;
};

/**
 * @brief Typed access to the SSM2603.
 *
 * Fixed setup: line input to the ADC, DAC to the headphone output, microphone off, I2S slave,
 * normal mode at 48 kHz with a 256 x fs master clock (12.288 MHz from the PL).
 *
 * Throws std::system_error when the bus reports an error, std::out_of_range for an argument
 * outside its register field and std::logic_error when used before Initialize().
 */
class Ssm2603
{
  public:
    /// 7-bit I2C address with CSB tied low, as on the Zybo Z7.
    static constexpr std::uint8_t kI2cAddress = 0x1A;

    static constexpr std::uint8_t kRegLeftLineIn = 0x00;
    static constexpr std::uint8_t kRegRightLineIn = 0x01;
    static constexpr std::uint8_t kRegLeftHeadphone = 0x02;
    static constexpr std::uint8_t kRegRightHeadphone = 0x03;
    static constexpr std::uint8_t kRegAnalogPath = 0x04;
    static constexpr std::uint8_t kRegDigitalPath = 0x05;
    static constexpr std::uint8_t kRegPower = 0x06;
    static constexpr std::uint8_t kRegDigitalInterface = 0x07;
    static constexpr std::uint8_t kRegSamplingRate = 0x08;
    static constexpr std::uint8_t kRegActive = 0x09;
    static constexpr std::uint8_t kRegReset = 0x0F;

    /// Highest headphone volume code; 0x79 is 0 dB.
    static constexpr std::uint8_t kHeadphoneVolumeMax = 0x7F;

    /// @p bus must outlive this object; it is not owned.
    explicit Ssm2603(I2cBus &bus) : bus_(bus) {}

    /// Resets the codec and brings it to the fixed setup above, leaving the DAC unmuted.
    void Initialize(const Ssm2603Config &config = Ssm2603Config{});

    /// For a codec another process already brought up with Initialize() and the same @p config:
    /// fills the shadow with the values Initialize() leaves behind, without any bus traffic, so
    /// SetHeadphoneVolume() and MuteDac() can be used. The part cannot be read back, so this
    /// trusts the caller.
    void AssumeConfigured(const Ssm2603Config &config = Ssm2603Config{});

    /// Deactivates the digital interface and powers the whole codec down.
    void Shutdown();

    /// Sets both headphone channels. @p volume is the 7-bit code, 0x79 = 0 dB.
    void SetHeadphoneVolume(std::uint8_t volume);

    void MuteDac(bool mute);

    /// Last value written to register @p reg (0 if never written). Throws std::out_of_range for
    /// registers above 0x0F.
    [[nodiscard]] std::uint16_t ShadowRegister(std::uint8_t reg) const;

    [[nodiscard]] bool IsInitialized() const
    {
        return initialized_;
    }

  private:
    static constexpr std::size_t kShadowSize = 16;

    void WriteRegister(std::uint8_t reg, std::uint16_t value);
    void RequireInitialized() const;

    I2cBus &bus_;
    std::array<std::uint16_t, kShadowSize> shadow_{};
    bool initialized_ = false;
};

} // namespace satlink::hal
