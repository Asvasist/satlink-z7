/**
 * @file test_regdef.cpp
 * @brief Tests for the constexpr register descriptors and the generated C++ headers.
 *
 * Most checks are static_asserts: a wrong register map fails at compile time.
 *
 * @verifies SRS-ICD-001
 */
#include <cstdint>
#include <gtest/gtest.h>

#include "satlink/regdef.hpp"
#include "satlink/regs/ccsds_frame_accel.hpp"
#include "satlink/regs/payload_ctrl.hpp"
#include "satlink/regs/spec_tap.hpp"

namespace {

namespace pc = satlink::regs::payload_ctrl;
namespace st = satlink::regs::spec_tap;
using satlink::regs::Access;
using satlink::regs::Field;

// Compile-time checks against the ICD.
static_assert(pc::kSpan == 0x24U);
static_assert(pc::ctrl::kDigLoopback.mask() == 0x2U);
static_assert(pc::tx_pinc::kPinc.mask() == 0xFFFFFFFFU);
static_assert(pc::atten::kReg.reset == 0x7FFFU);
static_assert(pc::status::kReg.access == Access::kWrite1Clear);
static_assert(st::status::kBusy.access == Access::kReadOnly);
static_assert(st::status::kFrameDropped.access == Access::kWrite1Clear);
static_assert(pc::version::kMajor.get(pc::version::kReg.reset) == 2U);

TEST(FieldTest, MaskCoversExactlyTheFieldBits)
{
    constexpr Field field{0x0U, 4U, 3U, Access::kReadWrite};
    EXPECT_EQ(0x70U, field.mask());
}

TEST(FieldTest, SetPreservesOtherBitsAndDropsExcessValueBits)
{
    constexpr Field field{0x0U, 4U, 3U, Access::kReadWrite};
    EXPECT_EQ(0xFFFFFFAFU, field.set(0xFFFFFFFFU, 0x2U));
    EXPECT_EQ(0x00000070U, field.set(0x0U, 0xFFU));
}

TEST(FieldTest, GetExtractsTheField)
{
    EXPECT_EQ(0x1234U, pc::rx_frame_len::kLen.get(0xABCD1234U));
}

TEST(FieldTest, FitsChecksTheValueRange)
{
    EXPECT_TRUE(pc::noise_level::kLevel.fits(0xFFFFU));
    EXPECT_FALSE(pc::noise_level::kLevel.fits(0x10000U));
    EXPECT_TRUE(pc::rx_sample_cnt::kCount.fits(0xFFFFFFFFU));
}

TEST(GeneratedHeaderTest, FrameAccelCrcFieldIs16BitsWide)
{
    namespace fa = satlink::regs::ccsds_frame_accel;
    EXPECT_EQ(0x0CU, fa::crc::kValue.offset);
    EXPECT_EQ(16U, fa::crc::kValue.width);
    EXPECT_EQ(0xFFFFU, fa::crc::kValue.get(fa::crc::kReg.reset));
}

} // namespace
