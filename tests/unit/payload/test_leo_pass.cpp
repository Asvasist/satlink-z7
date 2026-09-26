/**
 * @file test_leo_pass.cpp
 * @brief LEO pass geometry and link budget.
 *
 * @verifies SRS-ACM-002
 */
#include <cmath>
#include <gtest/gtest.h>

#include "satlink/payload/leo_pass.hpp"

namespace satlink::payload {
namespace {

TEST(LeoPassTest, OrbitalPeriodOf550KmOrbit)
{
    const LeoPass pass(PassConfig{});
    EXPECT_NEAR(95.6 * 60.0, pass.OrbitalPeriodS(), 30.0);
}

TEST(LeoPassTest, CulminatesAtTheRequestedElevationAndIsSymmetric)
{
    PassConfig cfg;
    cfg.max_elevation_deg = 45.0;
    const LeoPass pass(cfg);
    EXPECT_NEAR(45.0, pass.At(0.0).elevation_deg, 0.05);
    EXPECT_NEAR(pass.At(-120.0).elevation_deg, pass.At(120.0).elevation_deg, 1e-6);
    EXPECT_LT(pass.At(-60.0).range_rate_km_s, 0.0); // approaching
    EXPECT_GT(pass.At(60.0).range_rate_km_s, 0.0);  // receding
    EXPECT_NEAR(0.0, pass.At(0.0).range_rate_km_s, 0.05);
}

TEST(LeoPassTest, OverheadPassGeometryAndBudget)
{
    PassConfig cfg;
    cfg.max_elevation_deg = 90.0;
    cfg.zenith_esn0_db = 18.0;
    const LeoPass pass(cfg);
    const auto top = pass.At(0.0);
    EXPECT_NEAR(550.0, top.range_km, 1.0);
    EXPECT_NEAR(18.0 - 0.3, top.esn0_db, 0.05); // only the atmospheric loss at zenith
    // At the 5 degree mask the slant range is
    // sqrt((R + h)^2 - (R cos 5)^2) - R sin 5 = 2205 km: 12 dB more spreading loss.
    const auto aos = pass.At(pass.AosS());
    EXPECT_NEAR(5.0, aos.elevation_deg, 0.05);
    EXPECT_NEAR(2205.0, aos.range_km, 5.0);
    EXPECT_LT(aos.esn0_db, top.esn0_db - 12.0);
    // A 550 km overhead pass above 5 degrees lasts about 10 minutes.
    EXPECT_NEAR(600.0, pass.DurationS(), 40.0);
    EXPECT_FALSE(pass.At(pass.AosS() - 10.0).visible);
    EXPECT_TRUE(pass.At(pass.AosS() + 10.0).visible);
}

TEST(LeoPassTest, PassBelowTheMaskNeverRises)
{
    PassConfig cfg;
    cfg.max_elevation_deg = 3.0;
    const LeoPass pass(cfg);
    EXPECT_EQ(0.0, pass.DurationS());
}

TEST(LeoPassTest, ChannelSettingFollowsEsN0)
{
    const auto ch = ChannelFor(20.0, true);
    EXPECT_EQ(410, ch.noise_level); // sigma 0.1 * 4096
    EXPECT_EQ(0x7FFF, ch.gain_q15);
    EXPECT_EQ(0, ChannelFor(20.0, false).gain_q15);
    EXPECT_EQ(65535, ChannelFor(-40.0, true).noise_level); // clamped
}

} // namespace
} // namespace satlink::payload
