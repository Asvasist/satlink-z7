/**
 * @file test_conv.cpp
 * @brief CCSDS convolutional code: encoder vectors, puncturing lengths, decoding at every rate,
 *        error correction with hard and soft inputs.
 *
 * @verifies SRS-MDM-001
 */
#include <cstdint>
#include <gtest/gtest.h>
#include <random>
#include <vector>

#include "satlink/modem/conv.h"

namespace {

std::vector<std::uint8_t> RandomBytes(std::size_t n, unsigned seed)
{
    std::mt19937 gen(seed);
    std::vector<std::uint8_t> v(n);
    for (auto &b : v)
    {
        b = static_cast<std::uint8_t>(gen());
    }
    return v;
}

std::vector<std::int8_t> ToSoft(const std::vector<std::uint8_t> &bits, std::int8_t amplitude)
{
    std::vector<std::int8_t> soft(bits.size());
    for (std::size_t i = 0; i < bits.size(); ++i)
    {
        soft[i] = bits[i] != 0 ? static_cast<std::int8_t>(-amplitude) : amplitude;
    }
    return soft;
}

TEST(ConvTest, AllZeroInputGivesInvertedSecondOutput)
{
    const std::vector<std::uint8_t> zeros(4, 0);
    std::vector<std::uint8_t> out(satlink_conv_coded_bits(32, SATLINK_CONV_RATE_1_2));
    ASSERT_EQ(76U, out.size()); // 2 * (32 + 6)
    ASSERT_EQ(SATLINK_OK,
              satlink_conv_encode(zeros.data(), 32, SATLINK_CONV_RATE_1_2, out.data(), out.size()));
    for (std::size_t i = 0; i < out.size(); i += 2)
    {
        EXPECT_EQ(0, out[i]);
        EXPECT_EQ(1, out[i + 1]); // CCSDS inverts C2
    }
}

TEST(ConvTest, ImpulseResponseMatchesGenerators)
{
    // A single 1 followed by zeros: C1 walks G1 = 1111001, C2 walks the inverse of G2 = 1011011.
    const std::uint8_t in[1] = {0x80};
    std::vector<std::uint8_t> out(satlink_conv_coded_bits(8, SATLINK_CONV_RATE_1_2));
    ASSERT_EQ(SATLINK_OK,
              satlink_conv_encode(in, 8, SATLINK_CONV_RATE_1_2, out.data(), out.size()));
    const int g1[7] = {1, 1, 1, 1, 0, 0, 1};
    const int g2[7] = {1, 0, 1, 1, 0, 1, 1};
    for (std::size_t i = 0; i < 7; ++i)
    {
        EXPECT_EQ(g1[i], out[2 * i]) << i;
        EXPECT_EQ(1 - g2[i], out[2 * i + 1]) << i;
    }
}

TEST(ConvTest, PuncturedLengths)
{
    // 1046 trellis steps (1040 info + 6 tail).
    EXPECT_EQ(2092U, satlink_conv_coded_bits(1040, SATLINK_CONV_RATE_1_2));
    EXPECT_EQ(1569U, satlink_conv_coded_bits(1040, SATLINK_CONV_RATE_2_3));
    EXPECT_EQ(1395U, satlink_conv_coded_bits(1040, SATLINK_CONV_RATE_3_4));
    EXPECT_EQ(1256U, satlink_conv_coded_bits(1040, SATLINK_CONV_RATE_5_6));
    EXPECT_EQ(1196U, satlink_conv_coded_bits(1040, SATLINK_CONV_RATE_7_8));
    EXPECT_EQ(0U, satlink_conv_coded_bits(1040, SATLINK_CONV_RATE_COUNT));
}

class ConvRateTest : public ::testing::TestWithParam<satlink_conv_rate_t>
{};

TEST_P(ConvRateTest, NoiselessRoundTrip)
{
    const auto rate = GetParam();
    const auto data = RandomBytes(130, 7);
    std::vector<std::uint8_t> coded(satlink_conv_coded_bits(1040, rate));
    ASSERT_EQ(SATLINK_OK, satlink_conv_encode(data.data(), 1040, rate, coded.data(), coded.size()));
    const auto soft = ToSoft(coded, 64);
    std::vector<std::uint8_t> out(130);
    std::uint32_t metric = 0;
    ASSERT_EQ(SATLINK_OK,
              satlink_conv_decode(soft.data(), soft.size(), 1040, rate, out.data(), &metric));
    EXPECT_EQ(data, out);
    EXPECT_GT(metric, 0U);
}

TEST_P(ConvRateTest, CorrectsSparseHardErrors)
{
    const auto rate = GetParam();
    const auto data = RandomBytes(130, 11);
    std::vector<std::uint8_t> coded(satlink_conv_coded_bits(1040, rate));
    ASSERT_EQ(SATLINK_OK, satlink_conv_encode(data.data(), 1040, rate, coded.data(), coded.size()));
    // Isolated errors, spaced to suit the free distance (10 at rate 1/2, 3 at rate 7/8).
    const std::size_t spacing = rate == SATLINK_CONV_RATE_7_8 ? 120 : 40;
    for (std::size_t i = 13; i < coded.size(); i += spacing)
    {
        coded[i] ^= 1U;
    }
    const auto soft = ToSoft(coded, 1);
    std::vector<std::uint8_t> out(130);
    ASSERT_EQ(SATLINK_OK,
              satlink_conv_decode(soft.data(), soft.size(), 1040, rate, out.data(), nullptr));
    EXPECT_EQ(data, out);
}

INSTANTIATE_TEST_SUITE_P(AllRates, ConvRateTest,
                         ::testing::Values(SATLINK_CONV_RATE_1_2, SATLINK_CONV_RATE_2_3,
                                           SATLINK_CONV_RATE_3_4, SATLINK_CONV_RATE_5_6,
                                           SATLINK_CONV_RATE_7_8));

TEST(ConvTest, SoftDecisionsBeatHardDecisionsOnBurst)
{
    const auto data = RandomBytes(130, 3);
    std::vector<std::uint8_t> coded(satlink_conv_coded_bits(1040, SATLINK_CONV_RATE_1_2));
    ASSERT_EQ(SATLINK_OK, satlink_conv_encode(data.data(), 1040, SATLINK_CONV_RATE_1_2,
                                              coded.data(), coded.size()));
    // A burst of 8 wrong bits: with confidence 1 (hard) it breaks the code; flagged as low
    // confidence (the demapper saw them near the decision boundary) it is corrected.
    auto soft = ToSoft(coded, 40);
    for (std::size_t i = 500; i < 508; ++i)
    {
        soft[i] = static_cast<std::int8_t>(coded[i] != 0 ? 3 : -3);
    }
    std::vector<std::uint8_t> out(130);
    ASSERT_EQ(SATLINK_OK, satlink_conv_decode(soft.data(), soft.size(), 1040, SATLINK_CONV_RATE_1_2,
                                              out.data(), nullptr));
    EXPECT_EQ(data, out);
}

TEST(ConvTest, ErasuresAreIgnored)
{
    const auto data = RandomBytes(16, 5);
    std::vector<std::uint8_t> coded(satlink_conv_coded_bits(128, SATLINK_CONV_RATE_1_2));
    ASSERT_EQ(SATLINK_OK, satlink_conv_encode(data.data(), 128, SATLINK_CONV_RATE_1_2, coded.data(),
                                              coded.size()));
    auto soft = ToSoft(coded, 50);
    for (std::size_t i = 0; i < soft.size(); i += 5)
    {
        soft[i] = 0;
    }
    std::vector<std::uint8_t> out(16);
    ASSERT_EQ(SATLINK_OK, satlink_conv_decode(soft.data(), soft.size(), 128, SATLINK_CONV_RATE_1_2,
                                              out.data(), nullptr));
    EXPECT_EQ(data, out);
}

TEST(ConvTest, ArgumentChecks)
{
    std::uint8_t buf[4] = {};
    std::int8_t soft[4] = {};
    EXPECT_EQ(SATLINK_ERR_NULL, satlink_conv_encode(nullptr, 8, SATLINK_CONV_RATE_1_2, buf, 4));
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_conv_encode(buf, 8, SATLINK_CONV_RATE_1_2, buf, 4));
    EXPECT_EQ(SATLINK_ERR_RANGE,
              satlink_conv_decode(soft, 4, 8, SATLINK_CONV_RATE_1_2, buf, nullptr));
    EXPECT_EQ(SATLINK_ERR_NULL,
              satlink_conv_decode(nullptr, 4, 8, SATLINK_CONV_RATE_1_2, buf, nullptr));
    std::vector<std::int8_t> big(2 * (SATLINK_CONV_MAX_INFO_BITS + 7) + 20);
    std::vector<std::uint8_t> out(300);
    EXPECT_EQ(SATLINK_ERR_RANGE,
              satlink_conv_decode(big.data(), big.size(), SATLINK_CONV_MAX_INFO_BITS + 1,
                                  SATLINK_CONV_RATE_1_2, out.data(), nullptr));
}

} // namespace
