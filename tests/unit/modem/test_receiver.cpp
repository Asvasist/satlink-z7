/**
 * @file test_receiver.cpp
 * @brief End-to-end link simulation: frames through pulse shaping, a channel with gain, phase,
 *        frequency and timing offsets and AWGN, the matched filter and the receiver.
 *
 * @verifies SRS-MDM-005
 * @verifies SRS-MDM-003
 * @verifies SRS-MDM-006
 */
#include <cmath>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <numeric>
#include <random>
#include <vector>

#include "satlink/modem/modcod.h"

#include "link_sim.hpp"

namespace satlink::test {
namespace {

std::vector<std::uint8_t> Payload(unsigned seed)
{
    std::mt19937 gen(seed);
    std::vector<std::uint8_t> v(SATLINK_FRAME_INFO_BYTES);
    for (auto &b : v)
    {
        b = static_cast<std::uint8_t>(gen());
    }
    return v;
}

TEST(RrcTest, UnitEnergyAndNyquist)
{
    float taps[SATLINK_RRC_TAPS];
    ASSERT_EQ(SATLINK_OK,
              satlink_rrc_design(SATLINK_RRC_ALPHA, SATLINK_MODEM_SPS, taps, SATLINK_RRC_TAPS));
    float energy = 0.0F;
    for (float t : taps)
    {
        energy += t * t;
    }
    EXPECT_NEAR(1.0F, energy, 1e-5F);
    // RRC * RRC is a Nyquist pulse: zero at multiples of the symbol period.
    const int n = static_cast<int>(SATLINK_RRC_TAPS);
    const int sps = static_cast<int>(SATLINK_MODEM_SPS);
    for (int k = 1; k <= 3; ++k)
    {
        float acc = 0.0F;
        for (int i = 0; i < n; ++i)
        {
            const int j = i + k * sps;
            if (j < n)
            {
                acc += taps[i] * taps[j];
            }
        }
        EXPECT_NEAR(0.0F, acc, 0.01F) << "lag " << k;
    }
    EXPECT_EQ(SATLINK_ERR_RANGE, satlink_rrc_design(0.35F, 8, taps, 64));
}

TEST(ChannelTest, NoiseHasTheRequestedVariance)
{
    satlink_channel_t ch;
    satlink_channel_init(&ch, 42);
    satlink_channel_set_esn0(&ch, 3.0F);
    std::vector<satlink_cf_t> s(20000, satlink_cf_t{0.0F, 0.0F});
    satlink_channel_apply(&ch, s.data(), s.size());
    double power = 0.0;
    for (const auto &x : s)
    {
        power += static_cast<double>(x.re * x.re + x.im * x.im);
    }
    power /= static_cast<double>(s.size());
    EXPECT_NEAR(std::pow(10.0, -0.3), power, 0.02);
}

class ModcodLinkTest : public ::testing::TestWithParam<std::uint8_t>
{};

TEST_P(ModcodLinkTest, NoiselessWithOffsets)
{
    LinkSim sim(3);
    sim.channel.gain = 0.37F;       // arbitrary level: the AGC must cope
    sim.channel.phase_rad = 2.1F;   // unknown carrier phase
    sim.channel.freq_cps = 2.0e-6F; // slight frequency offset
    sim.channel.delay = 0.43F;      // fractional timing offset
    sim.SendFiller(300);            // receiver starts unsynchronised
    const auto data = Payload(GetParam());
    for (int i = 0; i < 6; ++i)
    {
        sim.Send(GetParam(), SATLINK_FRAME_DATA, data);
    }
    sim.Flush();
    // The first frame may be lost while the loops converge; the rest must be perfect.
    ASSERT_GE(sim.frames.size(), 5U);
    for (std::size_t i = sim.frames.size() - 5; i < sim.frames.size(); ++i)
    {
        const auto &f = sim.frames[i];
        EXPECT_TRUE(f.crc_ok) << "frame " << i;
        EXPECT_EQ(GetParam(), f.header.modcod);
        EXPECT_EQ(0, std::memcmp(data.data(), f.info, data.size()));
        EXPECT_GT(f.esn0_db, 25.0F);
    }
}

INSTANTIATE_TEST_SUITE_P(AllModcods, ModcodLinkTest, ::testing::Values(0, 1, 2, 3, 4));

TEST(ReceiverTest, AdaptiveModcodChangesFrameByFrame)
{
    LinkSim sim(5);
    sim.channel.phase_rad = -0.8F;
    sim.SendFiller(200);
    const std::uint8_t sequence[] = {0, 4, 1, 3, 2, 0, 4};
    for (const auto mc : sequence)
    {
        sim.Send(mc, SATLINK_FRAME_IDLE, {});
    }
    sim.Flush();
    ASSERT_GE(sim.frames.size(), 6U);
    const std::size_t first = sim.frames.size() - 6;
    for (std::size_t i = 0; i < 6; ++i)
    {
        EXPECT_EQ(sequence[i + 1], sim.frames[first + i].header.modcod);
        EXPECT_TRUE(sim.frames[first + i].crc_ok);
        EXPECT_EQ(0U, sim.frames[first + i].bit_errors);
    }
}

struct NoisePoint
{
    std::uint8_t modcod;
    float esn0_db;
};

class NoisyLinkTest : public ::testing::TestWithParam<NoisePoint>
{};

TEST_P(NoisyLinkTest, FramesDecodeAtThresholdPlusOneDb)
{
    const auto p = GetParam();
    LinkSim sim(11U + p.modcod);
    sim.channel.phase_rad = 1.0F;
    sim.channel.delay = 0.25F;
    satlink_channel_set_esn0(&sim.channel, p.esn0_db);
    sim.SendFiller(300);
    const int frames = 25;
    for (int i = 0; i < frames; ++i)
    {
        sim.Send(p.modcod, SATLINK_FRAME_IDLE, {});
    }
    sim.Flush();
    int ok = 0;
    for (const auto &f : sim.frames)
    {
        ok += f.crc_ok ? 1 : 0;
    }
    EXPECT_GE(ok, frames - 3) << "modcod " << int{p.modcod} << " at " << p.esn0_db << " dB";
    // Es/N0 estimate within 1.5 dB of the truth.
    EXPECT_NEAR(p.esn0_db, satlink_rx_stats(&sim.rx).esn0_db, 1.5F);
}

INSTANTIATE_TEST_SUITE_P(Thresholds, NoisyLinkTest,
                         ::testing::Values(NoisePoint{0, 3.0F}, NoisePoint{1, 6.0F},
                                           NoisePoint{2, 9.0F}, NoisePoint{3, 12.5F},
                                           NoisePoint{4, 16.0F}));

TEST(ReceiverTest, IdleFramesMeasureBitErrorRate)
{
    LinkSim sim(21);
    satlink_channel_set_esn0(&sim.channel, 20.0F);
    for (int i = 0; i < 4; ++i)
    {
        sim.Send(1, SATLINK_FRAME_IDLE, {});
    }
    sim.Flush();
    const auto stats = satlink_rx_stats(&sim.rx);
    EXPECT_GE(stats.frames_ok, 3U);
    EXPECT_EQ(stats.frames_ok * 1024U, stats.bits_checked);
    EXPECT_EQ(0U, stats.bit_errors);
    EXPECT_TRUE(stats.locked);

    satlink_rx_clear_stats(&sim.rx);
    EXPECT_EQ(0U, satlink_rx_stats(&sim.rx).frames_ok);
    EXPECT_TRUE(satlink_rx_stats(&sim.rx).locked);
}

TEST(ReceiverTest, DeepFadeLosesFramesThenRecovers)
{
    LinkSim sim(31);
    satlink_channel_set_esn0(&sim.channel, 15.0F);
    for (int i = 0; i < 3; ++i)
    {
        sim.Send(1, SATLINK_FRAME_IDLE, {});
    }
    satlink_channel_set_esn0(&sim.channel, -10.0F); // signal buried in noise
    for (int i = 0; i < 3; ++i)
    {
        sim.Send(1, SATLINK_FRAME_IDLE, {});
    }
    const auto during = satlink_rx_stats(&sim.rx);
    satlink_channel_set_esn0(&sim.channel, 15.0F);
    for (int i = 0; i < 6; ++i)
    {
        sim.Send(1, SATLINK_FRAME_IDLE, {});
    }
    sim.Flush();
    const auto after = satlink_rx_stats(&sim.rx);
    EXPECT_LE(during.frames_ok, 4U);
    // The AGC needs about one frame to recover from the 25 dB noise burst.
    EXPECT_GE(after.frames_ok, during.frames_ok + 4U);
}

TEST(ReceiverTest, NoiseAloneProducesNoValidFrames)
{
    LinkSim sim(41);
    satlink_channel_set_esn0(&sim.channel, 0.0F);
    sim.channel.gain = 0.0F;
    sim.SendFiller(20000);
    for (const auto &f : sim.frames)
    {
        EXPECT_FALSE(f.crc_ok);
    }
    EXPECT_FALSE(satlink_rx_stats(&sim.rx).locked);
}

} // namespace
} // namespace satlink::test
