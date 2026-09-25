/**
 * @file test_mapper_frame.cpp
 * @brief Constellations, soft demapping, MODCOD table, frame header and frame layout.
 *
 * @verifies SRS-MDM-002
 * @verifies SRS-MDM-004
 */
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#include "satlink/modem/frame.h"
#include "satlink/modem/mapper.h"
#include "satlink/modem/modcod.h"
#include "satlink/modem/prbs.h"

namespace {

std::vector<std::uint8_t> AllLabels(unsigned bps)
{
    std::vector<std::uint8_t> bits;
    for (unsigned v = 0; v < (1U << bps); ++v)
    {
        for (unsigned b = 0; b < bps; ++b)
        {
            bits.push_back(static_cast<std::uint8_t>((v >> (bps - 1 - b)) & 1U));
        }
    }
    return bits;
}

class MapperTest : public ::testing::TestWithParam<satlink_modulation_t>
{};

TEST_P(MapperTest, UnitEnergyGrayAndSoftRoundTrip)
{
    const auto mod = GetParam();
    const unsigned bps = static_cast<unsigned>(mod);
    const auto bits = AllLabels(bps);
    std::vector<satlink_cf_t> sym(bits.size() / bps);
    ASSERT_EQ(SATLINK_OK, satlink_map(mod, bits.data(), bits.size(), sym.data(), sym.size()));
    for (const auto &s : sym)
    {
        EXPECT_NEAR(1.0F, s.re * s.re + s.im * s.im, 1e-5F);
        const auto sl = satlink_slice(mod, s);
        EXPECT_NEAR(s.re, sl.re, 1e-6F);
        EXPECT_NEAR(s.im, sl.im, 1e-6F);
    }
    std::vector<std::int8_t> soft(bits.size());
    ASSERT_EQ(SATLINK_OK,
              satlink_demap(mod, sym.data(), sym.size(), 4.0F, soft.data(), soft.size()));
    for (std::size_t i = 0; i < bits.size(); ++i)
    {
        EXPECT_EQ(bits[i] == 0, soft[i] > 0) << i;
    }
}

INSTANTIATE_TEST_SUITE_P(AllModulations, MapperTest,
                         ::testing::Values(SATLINK_MOD_BPSK, SATLINK_MOD_QPSK, SATLINK_MOD_8PSK));

TEST(MapperTest8psk, NeighboursDifferInOneBit)
{
    const auto bits = AllLabels(3);
    std::vector<satlink_cf_t> sym(8);
    ASSERT_EQ(SATLINK_OK, satlink_map(SATLINK_MOD_8PSK, bits.data(), 24, sym.data(), 8));
    for (unsigned a = 0; a < 8; ++a)
    {
        for (unsigned b = 0; b < 8; ++b)
        {
            const float d = std::hypot(sym[a].re - sym[b].re, sym[a].im - sym[b].im);
            if (d > 0.1F && d < 0.8F) // adjacent points (distance 0.765)
            {
                EXPECT_EQ(1, __builtin_popcount(a ^ b)) << a << " " << b;
            }
        }
    }
}

TEST(MapperTest, RejectsBadLengths)
{
    const std::uint8_t bits[4] = {};
    satlink_cf_t sym[4];
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_map(SATLINK_MOD_8PSK, bits, 4, sym, 4));
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_map(SATLINK_MOD_BPSK, bits, 4, sym, 3));
    std::int8_t soft[2];
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_demap(SATLINK_MOD_QPSK, sym, 2, 1.0F, soft, 2));
}

TEST(ModcodTest, TableIsOrderedBySpectralEfficiency)
{
    float last_eff = 0.0F;
    float last_thr = -100.0F;
    for (std::uint8_t id = 0; id < SATLINK_MODCOD_COUNT; ++id)
    {
        const auto *mc = satlink_modcod_get(id);
        ASSERT_NE(nullptr, mc);
        EXPECT_EQ(id, mc->id);
        EXPECT_GT(mc->info_bits_per_symbol, last_eff);
        EXPECT_GT(mc->esn0_threshold_db, last_thr);
        last_eff = mc->info_bits_per_symbol;
        last_thr = mc->esn0_threshold_db;
    }
    EXPECT_EQ(nullptr, satlink_modcod_get(SATLINK_MODCOD_COUNT));
}

TEST(FrameHeaderTest, RoundTripAndCrcDetectsSingleBitErrors)
{
    for (std::uint8_t mc = 0; mc < SATLINK_MODCOD_COUNT; ++mc)
    {
        for (const int seq_value : {0, 1, 77, 127})
        {
            const auto seq = static_cast<std::uint8_t>(seq_value);
            const satlink_frame_header_t h{mc, SATLINK_FRAME_IDLE, seq};
            const std::uint16_t bits = satlink_frame_header_encode(&h);
            satlink_frame_header_t out{};
            ASSERT_TRUE(satlink_frame_header_decode(bits, &out));
            EXPECT_EQ(mc, out.modcod);
            EXPECT_EQ(seq, out.seq);
            EXPECT_EQ(SATLINK_FRAME_IDLE, out.type);
            for (int b = 0; b < 16; ++b)
            {
                EXPECT_FALSE(
                    satlink_frame_header_decode(static_cast<std::uint16_t>(bits ^ (1U << b)), &out))
                    << "bit " << b;
            }
        }
    }
}

TEST(FrameTest, LengthsPerModcod)
{
    EXPECT_EQ(2092U, satlink_frame_payload_symbols(0)); // BPSK 1/2
    EXPECT_EQ(1046U, satlink_frame_payload_symbols(1)); // QPSK 1/2
    EXPECT_EQ(698U, satlink_frame_payload_symbols(2));  // QPSK 3/4: 1395 bits -> 698 symbols
    EXPECT_EQ(523U, satlink_frame_payload_symbols(3));  // 8PSK 2/3: 1569 bits
    EXPECT_EQ(419U, satlink_frame_payload_symbols(4));  // 8PSK 5/6: 1256 bits -> 419 symbols
    EXPECT_EQ(96U + 2092U + 4U * 16U, satlink_frame_symbols(0));
    EXPECT_EQ(96U + 523U + 16U, satlink_frame_symbols(3));
    EXPECT_EQ(0U, satlink_frame_symbols(7));
    EXPECT_LE(satlink_frame_symbols(0), SATLINK_FRAME_MAX_SYMBOLS);
}

TEST(FrameTest, BuildPlacesAsmHeaderAndPilots)
{
    std::vector<satlink_cf_t> sym(SATLINK_FRAME_MAX_SYMBOLS);
    std::size_t n = 0;
    const satlink_frame_header_t h{1, SATLINK_FRAME_IDLE, 5};
    ASSERT_EQ(SATLINK_OK, satlink_frame_build(&h, nullptr, sym.data(), sym.size(), &n));
    EXPECT_EQ(satlink_frame_symbols(1), n);
    const float *asm_sym = satlink_frame_asm_symbols();
    for (unsigned i = 0; i < 32; ++i)
    {
        EXPECT_EQ(asm_sym[i], sym[i].re);
    }
    EXPECT_EQ(-1.0F, asm_sym[3]); // 0x1A = 0001 1010: fourth bit is 1
    // Pilot block after 512 payload symbols.
    for (unsigned i = 0; i < 16; ++i)
    {
        EXPECT_EQ(1.0F, sym[96 + 512 + i].re);
        EXPECT_EQ(0.0F, sym[96 + 512 + i].im);
    }
}

TEST(FrameTest, BuildArgumentChecks)
{
    std::vector<satlink_cf_t> sym(100);
    std::size_t n = 0;
    satlink_frame_header_t h{0, SATLINK_FRAME_DATA, 0};
    EXPECT_EQ(SATLINK_ERR_NULL, satlink_frame_build(&h, nullptr, sym.data(), sym.size(), &n));
    h.type = SATLINK_FRAME_IDLE;
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_frame_build(&h, nullptr, sym.data(), sym.size(), &n));
}

TEST(PrbsTest, Pn9StartsAllOnesAndHasPeriod511)
{
    std::vector<std::uint8_t> seq(2 * 511 / 8 + 2);
    satlink_pn9_fill(seq.data(), seq.size());
    EXPECT_EQ(0xFF, seq[0]); // the register starts at all ones
    auto bit = [&](std::size_t i) { return (seq[i / 8] >> (7 - i % 8)) & 1; };
    for (std::size_t i = 0; i < 511; ++i)
    {
        ASSERT_EQ(bit(i), bit(i + 511)) << i;
    }
    const std::uint8_t a[2] = {0xF0, 0x01};
    const std::uint8_t b[2] = {0x0F, 0x01};
    EXPECT_EQ(8U, satlink_bit_errors(a, b, 2));
}

} // namespace
